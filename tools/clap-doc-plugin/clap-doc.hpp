// SPDX-License-Identifier: Apache-2.0
#ifndef __CLAP_DOC_HPP__
#define __CLAP_DOC_HPP__

#include <algorithm>
#include <format>
#include <map>
#include <ranges>
#include <string>
#include <string_view>

#include "clap.hpp"
#include "clap-plugin.hpp"

namespace clap::doc {
    void c_quote(std::string &out, const std::string &src)
    {
        std::ranges::for_each(
            src | std::views::split(std::string_view("\n")),
            [&out](auto const &r) { out += std::format(" * {}\n", clap::to_sv(r)); }
        );
    }

    enum class decl_kind {
        DECL_FUNC,
        DECL_STRUCT,
        DECL_UNION,
        DECL_ENUM,
        DECL_TYPEDEF,
        DECL_DEFINE,
    };

    static std::unordered_map<std::string_view, decl_kind> decl_kind_map {
        { std::string_view("function"), decl_kind::DECL_FUNC,    },
        { std::string_view("struct"),   decl_kind::DECL_STRUCT,  },
        { std::string_view("union"),    decl_kind::DECL_UNION,   },
        { std::string_view("enum"),     decl_kind::DECL_ENUM,    },
        { std::string_view("typedef"),  decl_kind::DECL_TYPEDEF, },
        { std::string_view("define"),   decl_kind::DECL_DEFINE,  }
    };

    // kind to kind name conversion
    std::string_view get_decl_kind_string(decl_kind kind)
    {
        for (const auto &[name, val]: decl_kind_map)
            if (val == kind)
                return name;

        return { "<invalid>" };
    }

    struct param {
        std::string         name;
        std::string         type;
        std::string         desc;
        bool                has_comment;
        bool                has_definition;

        param(param &&param)
            : name(std::move(param.name)),
                type(std::move(param.type)),
                desc(std::move(param.desc)),
                has_comment(param.has_comment),
                has_definition((param.has_definition))
        {}

        param() = default;
        param(std::string name) : name(name), has_comment(false), has_definition(false) {}

        const param &operator=(const param &&src)
        {
            this->has_comment = src.has_comment;
            this->has_definition = src.has_definition;
            return std::move(src);
        }

        const param &operator=(const param &src) = delete;

        bool is_valid() const { return has_comment == has_definition; }

        std::string type_fmt() const
        {
            return std::format("{}{}", type, type.ends_with("*") ? "" : " ");
        }

        operator std::string() const
        {
            std::string ret;

            if (name.length())  ret += std::format("@{}", name);
            if (type.length())  ret += std::format(" [{}]", type);
            if (desc.length())  ret += std::format(" '{}'", desc);
            if (has_comment)    ret.append(" [comment]");
            if (has_definition) ret.append(" [def]");

            return ret;
        }
    };

    enum class _parser_state {
        ST_HEADER,
        ST_DECL,
        ST_PARAMS,
        ST_DESC,
        ST_CONTEXT,
        ST_RETURN,
        ST_END
    };

    using parser_state = clap::enum_integral<_parser_state>;

    static constexpr parser_state ST_HEADER  = _parser_state::ST_HEADER;
    static constexpr parser_state ST_DECL    = _parser_state::ST_DECL;
    static constexpr parser_state ST_PARAMS  = _parser_state::ST_PARAMS;
    static constexpr parser_state ST_DESC    = _parser_state::ST_DESC;
    static constexpr parser_state ST_CONTEXT = _parser_state::ST_CONTEXT;
    static constexpr parser_state ST_RETURN  = _parser_state::ST_RETURN;
    static constexpr parser_state ST_END     = _parser_state::ST_END;

    // Documentation entry
    class entry : protected clap::no_copy_no_move {
    public:
        using reporter      = std::function<void(const std::string &, const param *)>;

    private:
        std::string         text;
        std::string         path;
        unsigned int        line;
        std::string         base_url;
        bool                valid;

        // parser state
        std::string_view    cursor;
        std::string         *cur_desc;
        parser_state        state = ST_HEADER;
        bool                multiline;
        size_t              column;
        decl_kind           kind = decl_kind::DECL_FUNC;

        // parsed contents
        using param_idx_map = std::map<std::string, unsigned int, clap::less_sv>;
        std::vector<param>  params;     // contains actual params, preserves order
        param_idx_map       param_idx;  // maps param names to param[] indices for rev lookup
        std::string         decl;       // allowed to be empty, for section headers etc [future]
        std::string         desc;
        std::string         summary;
        std::string         context;
        std::string         signature;  // declaration from the source code as-is
        param               returns;
        entry               *alias;

        reporter            error_reporter;   // error reporting callback
        bool                in_inline;

        ///////////////////////////////////////////////////////////////////
        // State management
        ///////////////////////////////////////////////////////////////////

        bool set_state(parser_state s)
        {
            if (s == ST_END && state != ST_HEADER) { state = s; return true; }

            constexpr enum_set<_parser_state> valid_transitions_main[] = {
                /* ST_HEADER  */{},
                /* ST_DECL    */{ ST_HEADER, ST_DECL },
                /* ST_PARAMS  */{ ST_DECL, ST_PARAMS },
                /* ST_DESC    */{ ST_DECL, ST_PARAMS },
                /* ST_CONTEXT */{ ST_DECL, ST_PARAMS, ST_DESC },
                /* ST_RETURN  */{ ST_DECL, ST_PARAMS, ST_DESC, ST_CONTEXT },
                /* ST_END     */{ ST_DECL, ST_PARAMS, ST_DESC, ST_CONTEXT, ST_RETURN },
            };

            constexpr enum_set<_parser_state> valid_transitions_inline[] = {
                /* ST_HEADER  */{},
                /* ST_DECL    */{},
                /* ST_PARAMS  */{ ST_HEADER, ST_PARAMS },
                /* ST_DESC    */{ ST_DESC, ST_PARAMS },
                /* ST_CONTEXT */{},
                /* ST_RETURN  */{},
                /* ST_END     */{ ST_HEADER, ST_PARAMS, ST_DESC },
            };

            if ((!in_inline && !valid_transitions_main[s].contains(state)) ||
                (in_inline  && !valid_transitions_inline[s].contains(state))) {
                clap::plugin::err(std::format("!!! bad state transition {} -> {}",
                    static_cast<long>(state), static_cast<long>(s)));
                return false;
            }

            static std::unordered_map<parser_state, std::function<void(entry &)>, std::hash<size_t>> st_setters {
                { ST_DECL,      [](entry &e) { e.cur_desc = &e.summary; } },
                { ST_PARAMS,    [](entry &e) { if (!e.in_inline) e.cur_desc = nullptr; } },
                { ST_DESC,      [](entry &e) { e.cur_desc = &e.desc; } },
                { ST_CONTEXT,   [](entry &e) { e.cur_desc = &e.context; } },
                { ST_RETURN,    [](entry &e) { e.cur_desc = &e.returns.desc; } },
            };

            if (const auto f = st_setters[s]) f(*this);

            state = s;

            return true;
        }

        ///////////////////////////////////////////////////////////////////
        // Comment parser
        ///////////////////////////////////////////////////////////////////

        // return non-whitespace token at the beginning of the cursor
        std::string_view skip_non_whitespace(bool cut = false)
        {
            auto ws = cursor.find_first_of(" \t");
            if (ws == cursor.npos) return { "" };

            auto ret = cursor.substr(0, ws);
            if (cut)
                cursor.remove_prefix(ws);

            return ret;
        }

        // return leading whitespaces at the beginning of the cursor
        std::string_view skip_whitespace(bool cut = false)
        {
            auto non_ws = cursor.find_first_not_of(" \t");
            if (non_ws == cursor.npos) return { "" };

            auto ret = cursor.substr(0, non_ws);
            if (cut)
                cursor.remove_prefix(non_ws);

            return ret;
        }

        // consume the summary:
        // - "<decl>() - <summary>" for functions
        // - "<decl_kind> <decl> - <summary>" for everything else
        bool consume_decl()
        {
            auto word = skip_non_whitespace(true);
            if (!word.length()) return false;

            std::string_view decl_parsed{};

            if (decl_kind_map.contains(word)) {
                auto type = decl_kind_map[word];

                skip_whitespace(true);
                decl_parsed = skip_non_whitespace(true);
            } else {
                if (!word.ends_with("()")) return false;

                word.remove_suffix(2);
                decl_parsed = word;
                kind = decl_kind::DECL_FUNC;
            }

            // if decl was specified, it must match the parsed decl
            if (decl.length() && decl != decl_parsed) {
                error_reporter(
                    std::format("Declaration {} doesn't match comment: {}", decl, decl_parsed),
                    nullptr
                );
                return false;
            }

            decl = std::string(decl_parsed);

            if (cursor.starts_with(" - "))
                cursor.remove_prefix(3);

            set_state(ST_DECL);

            auto ret = consume_to_eol();
            if (ret)
                set_state(ST_PARAMS);

            return ret;
        }

        // consume the header:
        // - "**\n" for multiline comments
        // - "**[^\n]" for oneliners
        // - skip headers with more than 2 asterisks for now
        //
        // XXX: handle "***" for sections
        bool consume_header()
        {
            if (!cursor.starts_with("/**"))     return false;
            if (cursor[3] == '*')               return false;
            if (cursor[3] == '\n')              multiline = true;
            else                                multiline = false;
            if (!multiline && cursor[3] != ' ') return false;

            cursor.remove_prefix(4);

            /* XXX */
            if (!in_inline) set_state(ST_DECL);
            else            set_state(ST_PARAMS);

            return true;
        }

        // consume the rest of the line into wherever cur_desc is
        // pointing to, depending on the state
        bool consume_to_eol()
        {
            if (state == ST_END)    return true;

            skip_whitespace(true);

            auto pos = cursor.find_first_of("\n\r");
            if (pos == cursor.npos) pos = cursor.length();

            auto sub = cursor.substr(0, pos);
            if (sub.ends_with("*/")) {
                sub.remove_suffix(2);
                if (multiline)
                    return true;
            }

            auto last = sub.find_last_not_of(" \n") + 1;
            sub.remove_suffix(sub.length() - last);

            // params -> description
            if (!sub.length() && state == ST_PARAMS)
                set_state(ST_DESC);

            if (cur_desc) {
                if (!sub.length() && cur_desc->length()) {
                    // a new paragraph
                    if (!cur_desc->ends_with("\n"))
                        cur_desc->append("\n\n");
                    else if (!cur_desc->ends_with("\n\n"))
                        cur_desc->append("\n");
                } else if (sub.starts_with("* ")) {
                    // RST bulletpoint
                    if (!cur_desc->ends_with("\n"))
                        cur_desc->append("\n");
                } else if (cur_desc->length()) {
                    // adding to a non-empty cur_desc
                    if (!cur_desc->ends_with("\n"))
                        cur_desc->append(" ");
                }

                cur_desc->append(sub);
            }

            return true;
        }

        // cursor is at param name: "@<name>:"
        bool consume_param()
        {
            skip_whitespace(true);

            auto name = skip_non_whitespace(true);
            if (name.starts_with("@") && name.ends_with(":")) {
                name.remove_prefix(1);
                name.remove_suffix(1);
            } else {
                return false;
            }

            if (!in_inline && param_idx.contains(name)) return false;

            auto &p = (*this)[name];
            p.has_comment = true;

            set_state(ST_PARAMS);
            cur_desc = &p.desc;

            return consume_to_eol();
        }

        // consume "Return:" header
        bool consume_return()
        {
            skip_whitespace(true);

            // eat "Return:"
            skip_non_whitespace(true);
            returns.has_comment = true;
            set_state(ST_RETURN);

            return consume_to_eol();
        }

        // consume "Context:" header
        bool consume_context()
        {
            skip_whitespace(true);

            // eat "Context:"
            skip_non_whitespace(true);
            set_state(ST_CONTEXT);

            return consume_to_eol();
        }

        bool consume_line()
        {
            skip_whitespace(true);

            /* consume headers */
            if (cursor.starts_with("Context:")) return consume_context();
            if (cursor.starts_with("Return:"))  return consume_return();

            // "@" at the beginning of the line only means "parameter name"
            // in ST_DECL, which means that it kicks off the parameter section
            // or in ST_PARAMS; in all other sections it doesn't have special
            // meaning (other than maybe decorating the token that it's attached
            // to, which is backends' problem, not parser's)
            if (cursor.starts_with("@") && (state == ST_PARAMS || state == ST_DECL))
                return consume_param();

            // Inline comment, no other headers found, fill in the cur_desc
            // which points to current parameter's desc
            if (in_inline)  return consume_to_eol();
            // oneliner, no other headers found: proceed to consume it as decl
            if (!multiline) return consume_decl() && consume_to_eol();

            // In ST_DECL: if it's not any of the headers, it's a description,
            // any other state: continue as they are (multiline Context, Return
            // or description).
            if (state == ST_DECL)
                set_state(ST_DESC);

            return consume_to_eol();
        }

        bool consume_oneline()
        {
            if (multiline)
                return true;

            return consume_line();
        }

        bool consume_asterisk()
        {
            /* asterisk starting from line 2 */
            auto pos = skip_whitespace(false).length();
            if (cursor[pos] != '*') return false;

            // column==0 means line 2, it now gets set to pos, otherwise
            // column/pos mismatch means asterisks are not vertically aligned
            if (pos != column) {
                if (column)
                    error_reporter(
                        std::format("Warning: asterisks are not aligned: {} != {}", pos, column),
                        nullptr
                    );
                column = pos;
            }

            // asterisks on the left have to be in a vertical column
            cursor.remove_prefix(pos + 1);

            // the forward slash of a closing comment
            if (cursor == "/") {
                cursor.remove_prefix(1);
                set_state(ST_END);
                return true;
            }

            // strip away whitespaces
            skip_whitespace(true);

            return true;
        }

        using consumer = std::function<bool(std::string_view)>;

        // main comment parsing method
        void parse(std::string &source)
        {
            if (!source.size())
                return;

            state = ST_HEADER;
            std::vector<consumer> consumers = {
                // ST_HEADER
                [this](const std::string_view r) {
                    cursor = r;
                    bool ret = consume_header() && consume_oneline();
                    return ret;
                },
                // ST_DECL
                [this](const std::string_view r) {
                    cursor = r;
                    bool ret = consume_asterisk() && consume_decl();
                    return ret;
                },
            };

            // ST_PARAMS..ST_END
            for (int st: std::views::iota(static_cast<int>(ST_PARAMS), static_cast<int>(ST_END)))
                consumers.push_back(
                    [this, st](const std::string_view r) {
                        cursor = r;
                        bool ret = consume_asterisk() && consume_line();
                        return ret;
                    }
                );

            valid = std::ranges::all_of(
                source | std::views::split(std::string_view("\n")),
                [&](auto &&line_view) { return consumers[state](clap::to_sv(line_view)); }
            );

            if (!valid) {
                // Got a clap-doc header, but comment inside is not valid, report
                if (state != ST_HEADER)
                    // XXX: instead of state, print a meaningful message
                    error_reporter(
                        std::format("clap-doc comment for {} is not valid, state: {}", decl, static_cast<size_t>(state)),
                        nullptr
                    );

                // If parsing failed, clear the text, so validate() doesn't look further
                source.clear();
            }
        }

        // Keep param_idx[] up to date
        param_idx_map &param_idx_get()
        {
            if (param_idx.size() == params.size())
                return param_idx;

            param_idx.clear();

            auto i = 0;
            std::ranges::for_each(params, [this, &i](const auto &p) { param_idx[p.name] = i++; });

            return param_idx;
        }

        // Default error reporting callbacks
        static void error_reporter_noop(const std::string &desc, const param *p) {}
        static void error_reporter_debug(const std::string &desc, const param *p)
        {
            if (p) {
                if (p->has_comment && !p->has_definition)
                    llvm::dbgs()
                        << "Documented in " << desc
                        << " \"" << p->name << "\" "
                        << "doesn't exist in the signature"
                        << "\n";
                else if (!p->has_comment && p->has_definition)
                    llvm::dbgs()
                        << "In " << desc
                        << " \"" << p->type << " " << p->name << "\" "
                        << "is not documented"
                        << "\n";
            } else {
                llvm::dbgs() << desc << "\n";
            }
        }

    public:
        void validate()
        {
            if (!text.length())
                return;

            for (const auto &param: params)
                if (!param.is_valid()) {
                    error_reporter(
                        std::format("{} {}, parameter", get_decl_kind_string(kind), decl),
                        &param
                    );
                    valid = false;
                }

            if (kind == decl_kind::DECL_FUNC && !returns.is_valid()) {
                error_reporter(
                    std::format("{} {}, return", get_decl_kind_string(kind), decl),
                    &returns
                );
                valid = false;
            }
        }

        void alias_to(entry *acmt)
        {
            params.clear();
            param_idx.clear();
            desc.clear();
            acmt->alias = this;
        }

        ///////////////////////////////////////////////////////////////////
        // Accessors
        ///////////////////////////////////////////////////////////////////
        const clap::doc::decl_kind get_kind() const     { return kind; }
        const clap::doc::entry *get_alias() const       { return alias; }
        const std::string_view get_kind_string() const  { return get_decl_kind_string(kind); }
        const std::string_view get_decl() const         { return decl; }
        const std::string_view get_desc() const         { return desc; }
        const std::string_view get_summary() const      { return summary; }
        const std::string_view get_context() const      { return context; }
        const std::string_view get_signature() const    { return signature; }
        const std::string_view get_base_url() const     { return base_url; }
        const std::string_view get_path() const         { return path; }
        const unsigned int get_line() const             { return line; }
        bool is_valid() const                           { return valid; }
        const std::vector<param> &get_params() const    { return params; }

        // parameter accessor by name; empty key == return
        param &operator[](const std::string_view &key)
        {
            if (!key.length())
                return returns;

            auto &pmap = param_idx_get();

            auto it = pmap.find(key);
            if (it != pmap.end())
                return params[it->second];

            param p{ std::string(key) };

            pmap[p.name] = params.size();
            params.push_back(std::move(p));

            return params.back();
        }

        using param_key = std::pair<std::string_view, bool>;

        // parameter accessor by param_key:
        // - key.first: parameter name (string)
        // - key.second: write access (bool)
        param &operator[](const param_key &key)
        {
            auto &param = (*this)[key.first];

            if (key.second)
                param.has_definition = true;

            return param;
        }

        // An actual constructor
        entry(
            const std::string   &comment,
            const std::string   &decl,
            const std::string   &signature,
            decl_kind           kind = decl_kind::DECL_FUNC,
            std::string         path = "",
            unsigned int        line = 0,
            reporter            error_reporter = error_reporter_debug,
            const std::string   &base_url = BASE_URL
        )
            : text(comment),
                path(path),
                line(line),
                cursor(text),
                multiline(false),
                decl(decl),
                signature(signature),
                kind(kind),
                valid(false),
                cur_desc(nullptr),
                base_url(base_url),
                error_reporter(error_reporter),
                column(0)
        {
            parse(text);
        }

        // Append an in-line comment for struct/union/enum member.
        // It can be either
        // - a plain description, which will be added to the parameter
        // - one or more lines starting with "@member:", in which case
        //   each of the members will receive a corresponding description
        void append_inline(std::string &param, std::string &comment)
        {
            in_inline = true;
            column = 0;

            auto &p = (*this)[param];
            cur_desc = &p.desc;

            auto before_len = cur_desc->length();

            parse(comment);

            // Make sure that the comment covered *this* @param, before
            // setting its has_comment.
            if (before_len < cur_desc->length())
                p.has_comment = true;
        }
    };
} // namespace clap::doc

#endif // __CLAP_DOC_HPP__
