// SPDX-License-Identifier: Apache-2.0
#ifndef __CLAP_PLUGIN_BACKENDS_HPP__
#define __CLAP_PLUGIN_BACKENDS_HPP__

#include <string_view>
#include "llvm/Support/JSON.h"

#include "clap.hpp"
#include "clap-doc.hpp"
#include "clap-plugin.hpp"

namespace clap::plugin {
    ///////////////////////////////////////////////////////////////////////////
    // Output backends
    ///////////////////////////////////////////////////////////////////////////

    class backend;
    template<typename backend_t>
    concept is_backend = std::is_base_of<backend, backend_t>::value;

    // Abstract class for output backends; double times as a container for
    // all enabled backends.
    class backend : protected clap::no_copy_no_move {
    protected:
        streamer    &_streamer;
        options     &_options;

    public:
        backend(streamer &streamer, options &options) noexcept
            : _streamer(streamer),
              _options(options)
        {}
        virtual ~backend() noexcept = default;

        virtual bool emit(clap::doc::entry &entry) noexcept = 0;
        virtual void flush() noexcept = 0;

        static const std::string_view name()    { return { "<invalid>" }; }

        // The backend storage part goes below
        static std::unordered_map<std::string_view, std::unique_ptr<backend>> bmap;

        template<typename backend_t>
        requires is_backend<backend_t>
        static void make(streamer &streamer, options &options)
        {
            bmap[backend_t::name()] = std::make_unique<backend_t>(streamer, options);
        }

        static void push(clap::doc::entry &entry)
        {
            std::ranges::for_each(bmap, [&entry](const auto &b) { b.second->emit(entry); });
        }

        static void done()  { bmap.clear(); }
    };

    std::unordered_map<std::string_view, std::unique_ptr<backend>> backend::bmap;

    // JSON output backend: generates a <translation_unit>.json with an array
    // of all documented and undocumented declarations.
    class json_backend : public backend {
        llvm::json::Array array;
        llvm::raw_fd_ostream *os;

        [[nodiscard]]
        llvm::json::Object param_to_json(const doc::param &p) const noexcept
        {
            llvm::json::Object pobj;

            // return doesn't need a name field
            if (p.name.length())    pobj["name"] = p.name;
            if (p.type.length())    pobj["type"] = p.type;
            if (p.desc.length())    pobj["desc"] = p.desc;
            pobj["has_comment"] = p.has_comment;
            pobj["has_definition"] = p.has_definition;

            return pobj;
        }

    public:
        static const std::string_view name()    { return { "json" }; }

        json_backend(streamer &streamer, options &options) noexcept
            : backend(streamer, options)
        {
            log("json backend initialized");
        }

        ~json_backend() noexcept override       { flush(); }

        bool emit(clap::doc::entry &entry) noexcept override
        {
            if (!os) {
                auto res = _streamer.stream_find_get(_streamer.rel_src_path, ".json");
                if (auto err = res.getError())
                    return false;
                os = res.get();
            }

            llvm::json::Object obj;
            obj["is_valid"] = entry.is_valid();
            obj["name"]     = clap::to_string(entry.get_decl());
            obj["kind"]     = clap::to_string(entry.get_kind_string());
            obj["summary"]  = clap::to_string(entry.get_summary());
            obj["path"]     = clap::to_string(entry.get_path());
            obj["line"]     = entry.get_line();
            if (auto s = clap::to_string(entry.get_signature()); s.length())    obj["sig"] = s;
            if (auto s = clap::to_string(entry.get_desc()); s.length())         obj["desc"] = s;
            if (auto s = clap::to_string(entry.get_context()); s.length())      obj["context"] = s;

            llvm::json::Array parr;
            std::ranges::for_each(entry.get_params(), [this, &parr](const auto &p) {
                parr.push_back(std::move(param_to_json(p)));
            });

            if (parr.size())                            obj["params"] = std::move(parr);
            if (auto &r = entry[""]; r.type.length())   obj["return"] = std::move(param_to_json(r));

            array.push_back(std::move(obj));

            return true;
        }

        void flush() noexcept override
        {
            if (!os)    return;

            *os << llvm::formatv("{0:2}", llvm::json::Value(std::move(array))) << "\n";
            // streamer owns all the streams
            os = nullptr;
        }
    };

    // C output backend: generates a <translation_unit>.missing.c with clap-doc
    // formatted comments (what the clap::doc parser found or empty otherwise)
    class c_backend : public backend {
        std::string entry_to_c(doc::entry &entry) const
        {
            std::string ret = "/**\n * ";
            auto kind = entry.get_kind();
            auto decl = clap::to_string(entry.get_decl());
            auto summary = clap::to_string(entry.get_summary());
            if (kind == doc::decl_kind::DECL_FUNC)
                ret += std::format("{}() - {}\n", decl, summary);
            else
                ret += std::format("{} {} - {}\n", get_decl_kind_string(kind), decl, summary);

    #define COMMENT_PFX (sizeof(" * @:") - 1)
    #define VERT_ALIGN 4
            // calculate param descriptions offset to align them vertically
            size_t off = 0;
            for (const auto &param: entry.get_params()) {
                size_t cur_off = COMMENT_PFX + param.name.length();
                size_t rem = VERT_ALIGN - (cur_off % VERT_ALIGN);
                off = std::max(cur_off + rem, off);
            }

            for (const auto &param: entry.get_params()) {
                ret += std::format(" * @{}:", param.name);
                for (int i = 0; i < off - param.name.length() - COMMENT_PFX; i++)
                    ret.append(" ");
                ret.append(param.desc.length() ? param.desc : "comment");
                ret.append("\n");
            }

            ret.append(" *\n");

            auto desc = clap::to_string(entry.get_desc());
            if (desc.length())
                doc::c_quote(ret, desc);
            else
                ret.append(" * Description\n");

            auto context = clap::to_string(entry.get_context());
            auto &returns = entry[""];
            if (entry.get_kind() == doc::decl_kind::DECL_FUNC) {
                if (context.length())
                    doc::c_quote(ret, "Context: " + context);
                else
                    ret.append(" * Context:\n");

                if (returns.desc.length())
                    doc::c_quote(ret, "Return: " + returns.desc);
                else
                    ret.append(" * Return:\n");
            }
            ret.append(" */\n");
            ret.append(entry.get_signature());
            ret.append("\n");

            return ret;
        }

    public:
        static const std::string_view name()  { return { "c" }; }

        c_backend(streamer &streamer, options &options) noexcept
            : backend(streamer, options)
        {
            // Would initialize @os stream here, but constructorns don't fail
            // and we're not throwing
            log("c backend initialized");
        }

        ~c_backend() override   { flush(); }

        bool emit(clap::doc::entry &entry) noexcept override
        {
            if (entry.is_valid()) return false;
            auto res = _streamer.stream_find_get(entry.get_path(), ".missing.c");
            if (auto err = res.getError())
                return false;
            *res.get() << entry_to_c(entry) << "\n";

            return true;
        }

        void flush() noexcept override {}
    };

    // Markdown output backend:
    // generates <source_file>.toc.md and <source_file>.body.md, for subsequent
    // stitching by the build system: all TOC entries go first, then all bodies.
    class markdown_backend : public backend {
        std::string source_url_link(clap::doc::entry &entry) const
        {
            return std::format("{}/{}#L{}", _options.base_url, entry.get_path(), entry.get_line());
        }

        static std::string markdown_link(clap::doc::entry &entry)
        {
            return entry.get_kind() == clap::doc::decl_kind::DECL_FUNC
                ? std::format("#{0}", entry.get_decl())
                : std::format("#{0}-{1}", entry.get_kind_string(), entry.get_decl());
        }

        static std::string markdown_link_quoted(clap::doc::entry &entry)
        {
            return std::format("{{{}}}", markdown_link(entry));
        }

        static void markdown_quote(std::string &out, const std::string_view &src)
        {
            std::string_view trimmed = src;
            auto trim = trimmed.find_last_not_of(" \n");
            if (trim != trimmed.npos && trim < trimmed.length())
                trimmed.remove_suffix(trimmed.length() - trim - 1);

            std::ranges::for_each(
                trimmed | std::views::split(std::string_view("\n")),
                [&out](auto const &r) { out += std::format("> {}\n", clap::to_sv(r)); }
            );

            out.append("\n");
        }

        // comment header for table of contents, in markdown
        static std::string to_markdown_toc(clap::doc::entry &entry)
        {
            return entry.get_kind() == clap::doc::decl_kind::DECL_FUNC
                ? std::format("- [{0}()]({1})\n", entry.get_decl(), markdown_link(entry))
                : std::format("- [{0} {1}]({2})\n", entry.get_kind_string(), entry.get_decl(),
                              markdown_link(entry));
        }

        // comment contents in markdown
        std::string to_markdown_body(clap::doc::entry &entry) const
        {
            std::string header = entry.get_kind() == clap::doc::decl_kind::DECL_FUNC
                ? std::format("`{0}()`", entry.get_decl())
                : std::format("`{0} {1}`", entry.get_kind_string(), entry.get_decl());

            std::string ret = std::format("# [{}]({})\n", header, source_url_link(entry));
            ret += std::format("{} - {}\n\n", header, entry.get_summary());
            ret += entry.get_alias()
                ? std::format("```c\n{}\n{}\n```\n", entry.get_signature(),
                              entry.get_alias()->get_signature())
                : std::format("```c\n{}\n```\n", entry.get_signature());

            const auto &desc = entry.get_desc();
            if (desc.length()) {
                ret.append("## Description\n");
                markdown_quote(ret, desc);
            }

            auto kind = entry.get_kind();
            auto &params = entry.get_params();
            if (params.size()) {
                if (kind == clap::doc::decl_kind::DECL_ENUM) {
                    ret.append("|enumerator|description|\n|:-|:-|\n");
                    for (const auto &param: params)
                        ret += std::format("|`{}`|{}|\n", param.name, param.desc);
                } else if (kind == clap::doc::decl_kind::DECL_DEFINE) {
                    ret.append("|argument|description|\n|:-|:-|\n");
                    for (const auto &param: params)
                        ret += std::format("|`{}`|{}|\n", param.name, param.desc);
                } else {
                    ret.append("|argument|type|description|\n|:-|:-:|:-|\n");
                    for (const auto &param: params)
                        ret += std::format("|`{}`|`{}`|{}|\n", param.name, param.type, param.desc);
                }
                ret.append("\n");
            }

            const auto &context = entry.get_context();
            if (context.length()) {
                ret.append("## Context\n");
                markdown_quote(ret, context);
            }

            const auto &returns = entry[""];
            if (returns.type.length() && returns.type != "void") {
                ret += std::format("## Return: `{}`\n", returns.type);
                markdown_quote(ret, returns.desc);
            }

            ret += std::format("Path: [`{}:{}`]({})\n", entry.get_path(), entry.get_line(),
                               source_url_link(entry));
            ret.append("\n---\n");

            return ret;
        }

        // per-source file stream map
        std::unordered_map<
            std::string_view,
            std::pair<
                llvm::raw_fd_ostream *, /* toc */
                llvm::raw_fd_ostream *  /* body */
            >
        > streams;

    public:
        static const std::string_view name()  { return { "markdown" }; }

        markdown_backend(streamer &streamer, options &options)
            : backend(streamer, options)
        {
            log("markdown backend initialized");
        }

        ~markdown_backend() override    { flush(); }

        bool emit(clap::doc::entry &entry) override
        {
            if (!entry.is_valid())          return false;

            std::string_view path = entry.get_path();
            if (!streams.contains(path)) {
                auto tok_res = _streamer.stream_find_get(entry.get_path(), ".toc.md");
                if (tok_res.getError())     return false;
                auto body_res = _streamer.stream_find_get(entry.get_path(), ".body.md");
                if (body_res.getError())    return false;

                streams[entry.get_path()] = { tok_res.get(), body_res.get() };
            }

            // Write markdown output in 2 files:
            // * <source>.toc.md:   for collated table of contents
            // * <source>.body.md:  for collated documentation
            *streams[path].first << to_markdown_toc(entry);
            *streams[path].second << to_markdown_body(entry);

            return true;
        }

        void flush() override   { streams.clear(); }
    };
} // namespace clap::plugin

#endif // __CLAP_PLUGIN_BACKENDS_HPP__
