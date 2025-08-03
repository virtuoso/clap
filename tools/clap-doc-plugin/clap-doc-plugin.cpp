/* SPDX-License-Identifier: Apache-2.0 */
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/AST/Comment.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "clap.hpp"
#include "clap-doc.hpp"
#include "clap-plugin.hpp"
#include "clap-plugin-backends.hpp"

// clap-doc documentation generator
//
// Inspired by kernel-doc that's widely used in linux kernel, this is "clap-doc"
// or "clapdoc". Most of the grammar is lifted from kernel-doc verbatim, because
// it's already pretty robust and there's no reason to improve on perfection.
// Unlike kernel-doc, the extraction and parsing of the comments is implemented
// as a clang plugin, because:
// - parsing C with regular expressions is ugh
// - lifting either perl or python implementation from linux would involve
//   carefully fetching them as a build-time dependency and not part of the clap
//   source code distribution because of GPLv2
// - any potential syntactical deviations from kernel-doc are under our control
// - LLVM/clang's API is very robust and unlike GCC doesn't infect the code base
//   with viral licensing
// - being plugged into the compilation process, the code VS comment mismatch
//   reports are proper compiler warnings, and with -Werror, errors
//
// Not all kernel-doc features are supported: references/decorations such as
// &struct struct_name etc, DOC: comments.

using namespace std::literals;

namespace clap::plugin {

    using comment_pair = std::pair<clang::SourceRange, std::string>;
    using comment_map = llvm::DenseMap<fileid_line, comment_pair>;
    using macro_loc = std::tuple<
        clang::FileID,
        unsigned int,   // start offset in file
        unsigned int,   // end offset in file
        std::string,    // macro name
        std::string     // expanded text
    >;
    using macro_map = llvm::DenseMap<clang::FileID, std::vector<macro_loc>>;

    class doc : public core {
        // Comment objects for main documentation rendering
        std::vector<std::unique_ptr<clap::doc::entry>>          clap_docs;
        // C formatted declaration with boilerplate comments
        std::vector<std::unique_ptr<clap::doc::entry>>          clap_docs_missing;
        // Map Decls to docs for reverse lookup
        llvm::DenseMap<const clang::Decl *, clap::doc::entry *> decl_map;
        // Custom warning ID
        unsigned int                    diag_id;
        clang::DiagnosticsEngine        &diag;
        // Error reporting callback
        clap::doc::entry::reporter      error_reporter;
        // Signature printing
        clang::PrintingPolicy           printing_policy;
        // Macros
        macro_map                       macros;
        // Comments collected by CommentHandler
        comment_map                     comments;
        // Topmost comment: for use between CommentHandler and PPCallbacks
        comment_pair                    comment;

        const std::unordered_map<std::string_view, std::function<void()>> backends {
            { "json"sv,     [this](){ backend::make<json_backend>(*streamer.get(), *options.get()); } },
            { "markdown"sv, [this](){ backend::make<markdown_backend>(*streamer.get(), *options.get()); } },
            { "c"sv,        [this](){ backend::make<c_backend>(*streamer.get(), *options.get()); } },
        };

        template<typename printable_t>
        std::string print_with_policy(printable_t &printable)
        {
            std::string ret;
            llvm::raw_string_ostream print_stream(ret);
            printable.print(print_stream, printing_policy);
            print_stream.flush();

            return ret;
        }

        // Make a clap::doc::entry object for Decl
        // Retrieve name, signature, source path and line and the comment;
        // Create a clap::doc::entry object out of these, which also parses
        // the comment if it exists.
        using decl_result = std::optional<std::unique_ptr<clap::doc::entry>>;
        decl_result decl_comment_make(clang::Decl *decl, clap::doc::decl_kind kind)
        {
            auto *named_decl = llvm::dyn_cast<clang::NamedDecl>(decl);
            auto name = named_decl->getNameAsString();

            file_loc fl{ named_decl->getLocation(), compiler };
            spell_loc spl{ fl };
            auto file_path = spl.path();

            if (!is_source_path(file_path)) return std::nullopt;

            auto path = get_rel_path(file_path);
            auto line = spl.line();

            if (is_excluded_path(file_path)) return std::nullopt;

            std::string sig;

            if (kind == clap::doc::decl_kind::DECL_FUNC) {
                // auto range = decl->getSourceRange();
                // auto raw_sig = clang::Lexer::getSourceText(
                //     clang::CharSourceRange::getTokenRange(range), src_manager, compiler.getLangOpts()).str();
                source_range sr { decl->getSourceRange(), compiler };
                auto raw_sig = file_range { sr }.extend_to_trailing_parens().source<&norm_range<file_loc>::as_token_range>();
                auto p_brace = raw_sig.find('{');
                auto p_semi  = raw_sig.find(';');
                size_t cut = std::min(
                    p_brace == raw_sig.npos ? raw_sig.size() : p_brace,
                    p_semi  == raw_sig.npos ? raw_sig.size() : p_semi
                );
                raw_sig = raw_sig.erase(cut);
                sig.append(raw_sig);
                if (sig.ends_with("\n"))
                    sig.pop_back();
            } else {
                sig = print_with_policy(*named_decl);
            }

            sig.append(";");

            clang::RawComment *raw_comment = ast_ctx.getRawCommentForDeclNoCache(decl);

            auto comment = raw_comment ? raw_comment->getRawText(src_manager).str() : "";

            return std::make_unique<clap::doc::entry>(
                comment, name, sig, kind, clap::to_string(path), line, error_reporter
            );
        }

        // Get the Decl's trailing, then, failing that, leading comment
        template<typename decl_t>
        std::optional<std::string> get_member_comment(decl_t *_decl)
        {
            auto member_decl = _decl->getCanonicalDecl();
            base_loc decl_bloc{ member_decl, compiler };
            file_loc decl_fl{ decl_bloc };
            spell_loc decl_spl{ decl_fl };

            auto decl_end_line = decl_spl.line();
            auto decl_end_col = decl_spl.col();

            auto fp = decl_fl.decompose();
            fileid_line key{ fp.fid, decl_end_line };

            // Look for a trailing comment first
            auto it = comments.find(key);
            if (it != comments.end()) {
                auto &[key, cpair] = *it;
                source_range srcr{ cpair.first, compiler };
                spell_range spr{ srcr };
                auto cmt_col = spr.begin().col();

                if (cmt_col > decl_end_col && cpair.second.starts_with("/**")) {
                    return cpair.second;
                }
            }

            base_loc decl_beg_bloc { _decl, compiler, /*use_begin_loc=*/true };
            file_loc decl_beg_fl { decl_beg_bloc };
            auto decl_off = decl_beg_fl.tok_begin().decompose().pos;

            auto *member_comment = ast_ctx.getCommentForDecl(member_decl, &preproc);
            if (!member_comment)    return std::nullopt;

            std::string comment;
            for (auto *it = member_comment->child_begin(); it != member_comment->child_end(); it++) {
                auto src_range = (*it)->getSourceRange();
                source_range cmt_srcr { src_range, compiler };
                file_range cmt_fr { cmt_srcr };

                auto cmt_next = cmt_fr.range.end.tok_next();
                if (!cmt_next) continue;

                auto cmt_next_off = cmt_next->decompose().pos;
                if (cmt_next_off > decl_off)    continue;

                source_range bridge_sr { cmt_next.value(), decl_beg_fl.tok_begin() };
                file_range bridge_fr { bridge_sr };
                std::string bridge = bridge_fr.range.source<&norm_range<file_loc>::as_char_range>();
                if (!bridge.starts_with("*/"))  continue;

                bridge.erase(0, 2);

                bool all_ws = std::ranges::all_of(
                    bridge,
                    [](char c) { return clang::isWhitespace(static_cast<unsigned char>(c));
                });

                if (!all_ws)    continue;

                comment = cmt_fr.source();
                comment.insert(0, "/**");
                comment.append("*/");

                // The last child is the only one that can really be adjacent
                // to the decl; the only way we could get this far is when
                // there's nothing but whitespace between the child comment and
                // the decl. This break is technically redundant, but signals
                // intention.
                break;
            }
            return comment;
        }

        // Starting points for searching macros[fid] vectors
        llvm::DenseMap<clang::FileID, unsigned int> lower_bounds;

        template<typename comparable_t>
        static bool in_range(comparable_t val, comparable_t floor, comparable_t ceil, bool inclusive = false)
        {
            if (inclusive)  return val >= floor && val <= ceil;
            return val >= floor && val < ceil;
        }

        template<typename loc_t>
        requires std::constructible_from<loc_t, base_loc>
        std::optional<std::string> loc_get_type(loc_t fl)
        {
            fileid_pos fp = fl.decompose();

            // Get the vector for our FileID
            auto it_vec = macros.find(fp.fid);
            if (it_vec == macros.end())     return std::nullopt;

            auto &vec = it_vec->second;
            if (vec.empty())                return std::nullopt;

            // Continue iterating from the last visited index
            auto idx = lower_bounds[fp.fid];
            // Wrap around at the end
            if (idx >= vec.size())              idx = 0;
            // Rewind if decl's pos is behind current macro range
            if (fp.pos < std::get<1>(vec[idx])) idx = 0;

            // Skip everything out of range
            while (idx < vec.size() &&
                   !in_range(fp.pos, std::get<1>(vec[idx]), std::get<2>(vec[idx])))
                idx++;

            if (idx == vec.size())  return std::nullopt;

            assert(in_range(fp.pos, std::get<1>(vec[idx]), std::get<2>(vec[idx])));

            // pos is in range of vec[idx]; now, because all other ranges are
            // nested, the smallest one would be the last one that still matches
            auto best = idx;
            while (idx < vec.size() &&
                   in_range(fp.pos, std::get<1>(vec[idx]), std::get<2>(vec[idx])))
                best = idx++;

            // best must be in range, advance lower_bounds
            lower_bounds[fp.fid] = best;

            auto &[_fid, start_pos, end_pos, name, text] = vec[best];
            assert(in_range(fp.pos, start_pos, end_pos));

            return text;
        }

        std::optional<bool> type_defined_by_macro(const clang::FieldDecl *member_decl)
        {
            auto tsi = member_decl->getTypeSourceInfo();
            // Inconclusive; proceed to macro expansions
            if (!tsi)  return std::nullopt;

            auto type_loc = tsi->getTypeLoc();
            // while (auto inner = type_loc.getNextTypeLoc()) type_loc = inner;
            for (auto inner = tsi->getTypeLoc(); inner; inner = inner.getNextTypeLoc()) type_loc = inner;

            // Look at the first token of the type's loc
            base_loc bl { type_loc.getBeginLoc(), compiler };
            bl.strip_macros();

            // If it's still a macro, it's defined by a macro body
            return bl.is_macro();
        }

        std::string func_ret_type(clang::FunctionDecl *decl)
        {
            file_loc fl = file_loc::from_decl_type(decl, compiler);
            fl.strip_macros();

            if (auto m = loc_get_type(fl); m.has_value())    return m.value();

            // Fallback: render return type from TypeLoc
            auto ftl = decl->getTypeSourceInfo()->getTypeLoc().castAs<clang::FunctionTypeLoc>();
            auto rtl = ftl.getReturnLoc();
            source_range srcr { rtl.getSourceRange(), compiler };
            file_range fr { srcr };
            auto ret = fr.source();
            if (ret.length())   return ret;

            return decl->getDeclaredReturnType().getAsString(printing_policy);
        }

        using rec_seen_set = llvm::SmallPtrSet<const clang::RecordDecl *, 16>;

        template <class FieldFn>
        static void walk_record_fields(const clang::RecordDecl *rec_decl,
                                       llvm::StringRef prefix,
                                       rec_seen_set &seen,
                                       FieldFn &&emit)
        {
            if (!rec_decl)  return;
            if (const auto *rec_def = rec_decl->getDefinition()) rec_decl = rec_def;
            if (!rec_decl)  return;

            // Avoid cycles
            if (!seen.insert(rec_decl).second)  return;

            for (const clang::FieldDecl *field_decl : rec_decl->fields()) {
                clang::QualType fqt = field_decl->getType();

                // Anonymous struct/union member: recurse "in place" (no extra prefix)
                if (field_decl->isAnonymousStructOrUnion()) {
                    if (const clang::RecordDecl *inner_decl = get_record_decl(fqt))
                        walk_record_fields(inner_decl, prefix, seen, emit);
                    continue;
                }

                // Regular field: report it
                if (emit(prefix, field_decl))   continue;

                // Named nested record defined here (not pointer): add "name." to prefix
                // and descend IF the inner declaration is inline
                if (const clang::RecordDecl *inner_decl = get_record_decl(fqt); inner_decl &&
                    inner_decl->getLexicalDeclContext() == field_decl->getParent()) {
                    std::string next = (prefix + field_decl->getName()).str();
                    next.push_back('.');
                    walk_record_fields(inner_decl, next, seen, emit);
                }
            }

            seen.erase(rec_decl);
        }

        std::pair<std::string, bool> decl_get_type(const clang::FieldDecl *_decl)
        {
            auto member_decl = _decl->getCanonicalDecl();

            std::string type = member_decl->getType().getAsString(printing_policy);

            if (!type_defined_by_macro(member_decl).value_or(true)) return { type, false };

            auto macro_res = loc_get_type(base_loc { member_decl, compiler });
            return { macro_res.value_or(type), macro_res.has_value() };
        }

        static void strip_type_wrappers(clang::QualType &qt)
        {
            for (;;) {
                if (const auto *at = qt->getAs<clang::AttributedType>())    { qt = at->getEquivalentType(); continue; }
                if (const auto *pt = qt->getAs<clang::ParenType>())         { qt = pt->getInnerType();      continue; }
                if (const auto *et = qt->getAs<clang::ElaboratedType>())    { qt = et->getNamedType();      continue; }
                if (const auto *ar = qt->getAsArrayTypeUnsafe())            { qt = ar->getElementType();    continue; }
                if (const auto *tt = qt->getAs<clang::TypedefType>())       { qt = tt->desugar();           continue; }
                break;
            }
        }

        static const clang::TagDecl *get_underlying_decl(clang::TypedefDecl *td)
        {
            clang::QualType qt = td->getUnderlyingType();

            strip_type_wrappers(qt);

            // struct/union
            if (const auto *rec_type = qt->getAs<clang::RecordType>()) return rec_type->getDecl();

            // enum
            if (const auto *enum_type = qt->getAs<clang::EnumType>())  return enum_type->getDecl();

            return nullptr;
        }

        static const clang::RecordDecl *get_record_decl(clang::QualType qt)
        {
            strip_type_wrappers(qt);

            if (auto *rec_type = qt->getAs<clang::RecordType>()) {
                clang::RecordDecl *rec_decl = rec_type->getDecl();
                if (const auto *rec_def = rec_decl->getDefinition())  return rec_def;
                return rec_decl;
            }

            return nullptr;
        }

        /* Decl dispatch table */
        using consumer = std::function<void(clang::Decl *)>;
        std::unordered_map<clang::Decl::Kind, consumer> decl_consumers;

        void decl_consumers_setup()
        {
            decl_consumers[clang::Decl::Kind::Function] = [this](clang::Decl *decl) {
                auto *func_decl = llvm::dyn_cast<clang::FunctionDecl>(decl);
                if (!func_decl->hasBody() && !func_decl->hasPrototype())
                    return;

                decl_result cmt_res = decl_comment_make(decl, clap::doc::decl_kind::DECL_FUNC);
                if (!cmt_res.has_value())   return;

                auto cmt = std::move(cmt_res.value());

                for (auto *param: func_decl->parameters()) {
                    auto name = param->getNameAsString();
                    auto type = param->getOriginalType().getAsString(printing_policy);
                    (*cmt)[{name, true}].name = name;
                    (*cmt)[{name, true}].type = type;
                }

                auto ret_type_name = func_ret_type(func_decl);
                if (!ret_type_name.length())
                    ret_type_name = func_decl->getDeclaredReturnType().getAsString(printing_policy);
                if (ret_type_name != "void")
                    (*cmt)[{""sv, true}].type = ret_type_name;

                cmt->validate();

                comment_push(decl, std::move(cmt));
            };
            decl_consumers[clang::Decl::Kind::Record] = [this](clang::Decl *decl) {
                auto *rec_decl = llvm::dyn_cast<clang::RecordDecl>(decl);
                // Skip forward declarations
                if (!rec_decl->isCompleteDefinition())      return;

                clap::doc::decl_kind kind;
                if (rec_decl->isStruct())       kind = clap::doc::decl_kind::DECL_STRUCT;
                else if (rec_decl->isUnion())   kind = clap::doc::decl_kind::DECL_UNION;
                else                            return;

                decl_result cmt_res = decl_comment_make(decl, kind);
                if (!cmt_res.has_value())       return;

                auto cmt = std::move(cmt_res.value());
                rec_seen_set seen_set;

                walk_record_fields(
                    rec_decl, "", seen_set,
                    [this, &cmt](llvm::StringRef prefix, const clang::FieldDecl *decl) {
                        auto name = decl->getNameAsString();
                        name.insert(0, prefix);

                        auto res = get_member_comment(decl);
                        if (res.has_value()) {
                            auto comment = res.value();
                            cmt->append_inline(name, comment);
                        }

                        auto [type, from_macro] = decl_get_type(decl);
                        (*cmt)[{name, true}].name = name;
                        (*cmt)[{name, true}].type = type;
                        return from_macro;
                    }
                );

                cmt->validate();

                comment_push(decl, std::move(cmt));
            };
            decl_consumers[clang::Decl::Kind::Typedef] = [this](clang::Decl *decl) {
                auto *typedef_decl = llvm::dyn_cast<clang::TypedefDecl>(decl);
                auto tag_decl = typedef_decl->getUnderlyingType()->getAsTagDecl();
                if (tag_decl && !tag_decl->isCompleteDefinition()) return;

                decl_result cmt_res = decl_comment_make(decl, clap::doc::decl_kind::DECL_TYPEDEF);
                if (!cmt_res.has_value())   return;

                auto cmt = std::move(cmt_res.value());

                const clang::Decl *alias = tag_decl;
                const auto *tnd = llvm::cast<clang::TypedefNameDecl>(typedef_decl);
                if (const auto *tag_decl = tnd->getAnonDeclWithTypedefName(true)) {
                    if (const auto *rec_decl = llvm::dyn_cast<clang::RecordDecl>(tag_decl))      alias = rec_decl;
                    else if (const auto *enum_decl = llvm::dyn_cast<clang::EnumDecl>(tag_decl))  alias = enum_decl;
                } else if (const auto *tag_decl = get_underlying_decl(typedef_decl)) {
                    if (const auto *rec_decl = llvm::dyn_cast<clang::RecordDecl>(tag_decl))      alias = rec_decl;
                    else if (const auto *enum_decl = llvm::dyn_cast<clang::EnumDecl>(tag_decl))  alias = enum_decl;
                }

                if (alias && decl_map.contains(alias))   cmt->alias_to(decl_map[alias]);

                cmt->validate();

                comment_push(decl, std::move(cmt));
            };
            decl_consumers[clang::Decl::Kind::Enum] = [this](clang::Decl *decl) {
                auto *enum_decl = llvm::dyn_cast<clang::EnumDecl>(decl);

                decl_result cmt_res = decl_comment_make(decl, clap::doc::decl_kind::DECL_ENUM);
                if (!cmt_res.has_value())   return;

                auto cmt = std::move(cmt_res.value());

                for (auto *en: enum_decl->enumerators()) {
                    auto name = en->getNameAsString();
                    auto res = get_member_comment(en);
                    if (res.has_value()) {
                        auto comment = res.value();
                        cmt->append_inline(name, comment);
                    }

                    (*cmt)[{name, true}].name = name;
                }

                cmt->validate();

                comment_push(decl, std::move(cmt));
            };
        }

public:
        doc(clang::CompilerInstance &ci, std::unique_ptr<struct options> _options)
            : core(ci, std::move(_options)),
              diag(ci.getDiagnostics()),
              printing_policy(ci.getLangOpts())
        {
            decl_consumers_setup();

            // Set up signature/type printing policy
            printing_policy.TerseOutput = true;
            printing_policy.SuppressInitializers = true;
            printing_policy.SuppressSpecifiers = false;
            printing_policy.SuppressTagKeyword = false;
            printing_policy.Bool = true;

            // Add warning/error reporter for use in clap_doc_comment, which
            // reports mismatches between comment and C declaration

            // Search clang command line for -Werror
            auto diag_opts = compiler.getDiagnosticOpts();
            bool werror = std::ranges::any_of(diag_opts.Warnings, [](const auto &opt) {
                return opt == "error";
            });

            // Issue errors instead of warnings if -Werror is specified
            diag_id = diag.getCustomDiagID(
                werror ? clang::DiagnosticsEngine::Error : clang::DiagnosticsEngine::Warning, "%0"
            );

            // clap-doc error reporting function, passed into clap_doc_comment constructor
            error_reporter = [this](const std::string &desc, const clap::doc::param *p) {
                if (p) {
                    if (p->has_comment && !p->has_definition)
                        diag.Report(diag_id) << std::format(
                            "Documented in {} \"{}\" doesn't exist in the signature",
                            desc, p->name
                        );
                    else if (!p->has_comment && p->has_definition)
                        diag.Report(diag_id) << std::format(
                            "In {}: parameter {} ({}) is not documented", desc, p->name, p->type
                        );
                } else {
                    diag.Report(diag_id) << desc;
                }
            };
        }
        ~doc() { log("### clap::plugin::doc goes buh-bye"); }

        [[nodiscard]] const
        clap::doc::entry::reporter              &get_reporter() const noexcept  { return error_reporter; }
        [[nodiscard]] clang::CompilerInstance   &get_compiler() const noexcept  { return compiler; }
        [[nodiscard]] clang::Preprocessor       &get_preproc() const noexcept   { return preproc; }
        [[nodiscard]] comment_pair              &get_comment() noexcept         { return comment; }
        [[nodiscard]] comment_map               &get_comment_map() noexcept     { return comments; }
        [[nodiscard]] macro_map                 &get_macro_map() noexcept       { return macros; }

        void consume(clang::Decl *decl)
        {
            auto kind = decl->getKind();
            if (!decl_consumers.contains(kind)) return;

            decl_consumers[kind](decl);
        }

        void comment_push(clang::Decl *decl, std::unique_ptr<clap::doc::entry> cmt)
        {
            if (cmt->is_valid()) {
                clap_docs.push_back(std::move(cmt));
                if (decl)   decl_map[decl] = clap_docs.back().get();
            } else {
                clap_docs_missing.push_back(std::move(cmt));
                if (decl)   decl_map[decl] = clap_docs_missing.back().get();
            }
        }

        void output()
        {
            std::ranges::for_each(backends, [this](const auto &b){ b.second(); });

            std::ranges::for_each(clap_docs, [](const auto &entry) {
                backend::push(*entry.get());
            });
            std::ranges::for_each(clap_docs_missing, [](const auto &entry) {
                backend::push(*entry.get());
            });
            backend::done();
        }
    };
} // namespace clap::plugin

namespace {

class clap_preproc_callbacks : public clang::PPCallbacks {
    clap::plugin::doc           *doc;
    clang::SourceManager        &src_manager;
    clap::plugin::comment_pair  &comment;
    clap::plugin::comment_map   &comments;
    clap::plugin::macro_map     &macros;
    clap::plugin::options       &options;

    // Reconstruct macro signature
    std::string rebuild_macro(llvm::StringRef name, const clang::MacroInfo *macro_info)
    {
        auto num_params = macro_info->getNumParams();
        std::string sig = num_params
            ? std::format("#define {}(", clap::to_string(name))
            : std::format("#define {}", clap::to_string(name));

        for (const auto *p: macro_info->params()) {
            sig.append(p->getName());
            if (p != macro_info->params().back())  sig.append(", ");
        }

        if (num_params) sig.append(")");

        return sig; // hope for NRVO
    }

public:
    clap_preproc_callbacks(clap::plugin::doc *doc)
        : doc(doc),
          options(doc->get_options()),
          src_manager(doc->get_compiler().getSourceManager()),
          comment(doc->get_comment()),
          comments(doc->get_comment_map()),
          macros(doc->get_macro_map())
    {}

    void MacroExpands(const clang::Token &name_tok,
                      [[maybe_unused]] const clang::MacroDefinition &def,
                      clang::SourceRange range,
                      [[maybe_unused]] const clang::MacroArgs *args) override
    {
        auto id_info = name_tok.getIdentifierInfo();
        auto name = id_info->getName();

        clap::plugin::source_range sr{ range, doc->get_compiler() };
        if (!sr) {
            clap::plugin::log("[", name, "] source range (", sr.str(), ") is not valid");
            return;
        }

        if (sr.is_virtual()) return;

        clap::plugin::file_range fr{ sr };
        if (!fr) { clap::plugin::err("file range (", fr.str(), ") is not valid"); return; }

        auto path = fr.range.begin.path();
        if (doc->is_excluded_path(path)) return;

        auto start_pos = fr.range.begin.decompose();
        auto end_pos = fr.range.end.decompose();

        macros[start_pos.fid].push_back(
            {
                start_pos.fid,
                start_pos.pos,
                end_pos.pos,
                name.str(),
                fr.source(),
            }
        );
    }

    void MacroDefined(const clang::Token &name_tok, const clang::MacroDirective *macro) override
    {
        auto macro_info = macro->getMacroInfo();
        auto id_info = name_tok.getIdentifierInfo();

        auto name = id_info->getName();
        auto loc = macro_info->getDefinitionLoc();
        clap::plugin::file_loc fl{ loc, doc->get_compiler() };
        clap::plugin::spell_loc sl{ fl };

        // Source file
        std::string_view file_path = sl.path();
        if (!file_path.length())    return;

        // Filter out external macros
        if (doc->is_excluded_path(file_path)) return;

        // Strip off src_root prefix
        file_path = doc->get_rel_path(file_path);

        auto line = sl.line();

        // This is not ideal, but filters out obvious garbage
        if (!comment.second.starts_with("/**"))     return;

        clap::plugin::source_range cmt_srcr { comment.first.getEnd(), doc->get_compiler() };
        clap::plugin::spell_loc cmt_spl { cmt_srcr.end };
        auto comment_line = cmt_spl.line();
        if (line - comment_line > 1 /* epsilon */)  return;

        auto sig = rebuild_macro(name, macro_info);

        auto cmt = std::make_unique<clap::doc::entry>(
            comment.second, name.str(), sig, clap::doc::decl_kind::DECL_DEFINE,
            clap::to_string(file_path), line, doc->get_reporter()
        );

        for (const auto *p: macro_info->params()) {
            auto p_name = p->getName();
            (*cmt)[{p_name, true}].name = p_name.str();
        }

        cmt->validate();

        doc->comment_push(nullptr, std::move(cmt));
    }
};

class clap_comment_handler : public clang::CommentHandler {
    std::shared_ptr<clap::plugin::doc>  doc;
    clap::plugin::comment_pair          &comment;
    clap::plugin::comment_map           &comments;
    clang::SourceManager                &src_manager;

public:
    clap_comment_handler(std::shared_ptr<clap::plugin::doc> doc)
        : doc(doc),
          comment(doc->get_comment()),
          comments(doc->get_comment_map()),
          src_manager(doc->get_compiler().getSourceManager())
    {}

protected:
    bool HandleComment([[maybe_unused]] clang::Preprocessor &preproc, clang::SourceRange range) override
    {
        clap::plugin::source_range srcr { range, doc->get_compiler() };
        clap::plugin::spell_range sr { srcr };
        clap::plugin::file_loc fl { srcr.begin };
        clap::plugin::spell_loc spl { fl };

        unsigned int line = spl.line();

        clap::plugin::fileid_line fil = { fl.decompose().fid, line };

        comments[fil] = { range, sr.source() };

        comment = comments[fil];

        return false;
    }
};

struct clap_consumer : clang::ASTConsumer {
    std::shared_ptr<clap::plugin::doc>      doc;
    std::unique_ptr<clap_comment_handler>   comment_handler;

public:
    clap_consumer(std::shared_ptr<clap::plugin::doc> doc) : doc(doc)
    {
        clap::plugin::log("[clapdoc] plugin loaded successfully");

        // LLVM API is wonderful: it wants to own some callbacks via unique_ptr<T>()
        // (PPCallbacks) and doesn't want to own others (CommentHandler), leaving
        // the joy of its lifetime management to the caller. Which would be fine, if
        // PluginASTAction's lifetime matched that of the Preprocessor, but, of
        // course, it get nuked right after initialization (which actually does make
        // sense, but also makes things inconvenient wrt the CommentHandler ownership).
        //
        // For this reason, make this one (ASTConsumer) own the CommentHandler object,
        // which is semantically painful, but we have towork with the APIs we're dealt.
        //
        // And since we're installing our CommentHandler here, do the same for
        // PPCallbacks for no other reason than consistency, even though we don't
        // actually own it.
        doc->get_preproc().addPPCallbacks(
            std::make_unique<clap_preproc_callbacks>(doc.get())
        );

        comment_handler = std::make_unique<clap_comment_handler>(doc);
        doc->get_preproc().addCommentHandler(comment_handler.get());
    }

    ~clap_consumer()
    {
        // Remove our CommentHandler from the Preprocessor, so that it doesn't
        // get a chance to access invalid memory, since (see the above comment)
        // we're stuck with its ownership, it's the correct thing to do.
        doc->get_preproc().removeCommentHandler(comment_handler.get());
    }

    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override
    {
        std::ranges::for_each(decl_group, [this](auto *decl) { doc->consume(decl); });

        return true;
    }

    void HandleTranslationUnit([[maybe_unused]] clang::ASTContext &ast_ctx) override
    {
        doc->output();
    }
};

class clap_action : public clang::PluginASTAction {
    std::unique_ptr<clap::plugin::options>  options;
    std::shared_ptr<clap::plugin::doc>      doc;

protected:
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef) override
    {
        doc = std::make_shared<clap::plugin::doc>(compiler, std::move(options));

        // Normally, plugins would install their PPCallbacks here, but because
        // LLVM's API is wildly inconsistent, we do it in the ASTConsumer instead.
        // See the comment in clap_consumer().

        return std::make_unique<clap_consumer>(doc);
    }

    bool ParseArgs([[maybe_unused]] const clang::CompilerInstance &compiler,
                   const std::vector<std::string> &args) override
    {
        options = std::make_unique<clap::plugin::options>(args);

        return true;
    }

    PluginASTAction::ActionType getActionType() override
    {
        return CmdlineAfterMainAction;
    }
};

} // namespace

static clang::FrontendPluginRegistry::Add<clap_action> x("clapdoc", "Extracts clap-doc comments from code");
