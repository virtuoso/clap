// SPDX-License-Identifier: Apache-2.0
#ifndef __CLAP_PLUGIN_HPP__
#define __CLAP_PLUGIN_HPP__

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "clap.hpp"
#include <concepts>
#include <ranges>
#include <string_view>

namespace clap::plugin {
    // clang::FileID + file position llvm::DenseMap<> key
    using fileid_pos = struct { clang::FileID fid; unsigned int pos; };
    // clang::FileID + line llvm::DenseMap<> key
    using fileid_line = struct { clang::FileID fid; unsigned int line; };
} // namespace clap::plugin

template<>
struct llvm::DenseMapInfo<clap::plugin::fileid_pos> {
    // NOLINTBEGIN(readability-identifier-naming)
    static inline constexpr clap::plugin::fileid_pos getEmptyKey()      { return { clang::FileID(), ~0u }; }
    static inline constexpr clap::plugin::fileid_pos getTombstoneKey()  { return { clang::FileID(), ~0u-1 }; }
    static unsigned getHashValue(const clap::plugin::fileid_pos &k)
    {
        return llvm::hash_combine(k.fid.getHashValue(), k.pos);
    }
    static bool isEqual(const clap::plugin::fileid_pos &a, const clap::plugin::fileid_pos &b)
    {
        return a.fid == b.fid && a.pos == b.pos;
    }
    // NOLINTEND(readability-identifier-naming)
};

template<>
struct llvm::DenseMapInfo<clap::plugin::fileid_line> {
    // NOLINTBEGIN(readability-identifier-naming)
    static inline clap::plugin::fileid_line getEmptyKey()     { return { clang::FileID(), ~0u }; }
    static inline clap::plugin::fileid_line getTombstoneKey() { return { clang::FileID(), ~0u-1 }; }
    static unsigned getHashValue(const clap::plugin::fileid_line &k)
    {
        return llvm::hash_combine(k.fid.getHashValue(), k.line);
    }
    static bool isEqual(const clap::plugin::fileid_line &a, const clap::plugin::fileid_line &b)
    {
        return a.fid == b.fid && a.line == b.line;
    }
    // NOLINTEND(readability-identifier-naming)
};

namespace clap::plugin {
    template<typename... Args>
    void log(Args&&... args)
    {
        if (!verbose) return;

        (llvm::errs() << ... << args) << "\n";
    }

    template<typename... Args>
    void err(Args&&... args)
    {
        (llvm::errs() << ... << args) << "\n";
    }

    //////////////////////////////////////////////////////////////////////////
    // Let's fix some of the more egregious LLVM API misfeatures
    //////////////////////////////////////////////////////////////////////////
    struct clang_context {
    protected:
        clang::CompilerInstance &compiler;
        clang::SourceManager    &src_manager;
        clang::LangOptions      &lang_opts;
    public:
        clang_context(clang::CompilerInstance &compiler) noexcept
            : compiler(compiler),
            src_manager(compiler.getSourceManager()),
            lang_opts(compiler.getLangOpts()) {}

        [[nodiscard]]
        clang::CompilerInstance &get_compiler() const noexcept  { return compiler; }
    };

    ///////////////////////////////////////////////////////////////////////////
    // Locations (SourceLocation wrappers)
    ///////////////////////////////////////////////////////////////////////////

    struct file_loc;

    // Dumb wrapper around clang::SourceLocation
    struct base_loc : public clang_context {
    private:
        clang::SourceLocation   loc;

    public:
        // loc to be initialized by the child constructor
        base_loc(clang::CompilerInstance &compiler) noexcept
            : clang_context(compiler) {}

        // Construct with *some* kind of SourceLocation (don't)
        base_loc(const clang::SourceLocation _loc, clang::CompilerInstance &compiler) noexcept
            : clang_context(compiler), loc(_loc) {}

        // Construct from a Decl's location (defaults to end)
        base_loc(const clang::Decl *_decl, clang::CompilerInstance &compiler, bool use_begin_loc = false) noexcept
            : clang_context(compiler),
              loc(src_manager.getFileLoc(use_begin_loc ? _decl->getBeginLoc() : _decl->getEndLoc()))
        {}

        // Get raw underlying SourceLocation (but, you know, don't)
        [[nodiscard]]
        clang::SourceLocation raw() const noexcept  { return loc; }

        [[nodiscard]]
        bool is_valid() const noexcept              { return loc.isValid(); }

        [[nodiscard]]
        bool is_macro() const noexcept              { return loc.isMacroID(); }

        [[nodiscard]]
        explicit operator bool() const noexcept     { return is_valid(); }

        [[nodiscard]]
        static bool is_virtual(clang::SourceLocation _loc, clang::SourceManager &src_manager) noexcept
        {
            return src_manager.isWrittenInBuiltinFile(_loc) ||
                   src_manager.isWrittenInCommandLineFile(_loc) ||
                   src_manager.isWrittenInScratchSpace(_loc);
        }

        [[nodiscard]]
        bool is_virtual() const noexcept            { return is_virtual(loc, src_manager); }

        [[nodiscard]]
        base_loc normalized() const noexcept
        {
            if (!loc.isValid())                     return *this;

            if (!loc.isMacroID()) {
                auto fl = src_manager.getFileLoc(loc);
                if (!is_virtual(fl, src_manager))   return { fl, compiler };
                return *this;
            }

            // First, expansion
            auto exp = src_manager.getExpansionLoc(loc);
            if (!is_virtual(exp, src_manager))      return { src_manager.getFileLoc(exp), compiler };

            // Then, spelling
            auto sp  = src_manager.getSpellingLoc(loc);
            if (!is_virtual(sp, src_manager))       return { src_manager.getFileLoc(sp),  compiler };

            // If all else fails, raw, to avoid slow case assert
            return *this;
        }
    
        [[nodiscard]]
        std::string str() const noexcept            { return loc.printToString(src_manager); }

        // Decompose into FileID + file position pair
        [[nodiscard]]
        fileid_pos decompose() const noexcept
        {
            auto decomp = src_manager.getDecomposedLoc(loc);
            return fileid_pos{ decomp.first, decomp.second };
        }

        // Recursively remove macro wrappers at location
        void strip_macros() noexcept
        {
            while (is_valid() && is_macro() && src_manager.isMacroArgExpansion(loc))
                loc = src_manager.getImmediateSpellingLoc(loc);
        }
    };

    struct norm_loc : public clang_context {
    protected:
        clang::SourceLocation loc;
    public:
        norm_loc(const clang::SourceLocation _loc, clang::CompilerInstance &compiler) noexcept
            : clang_context(compiler), loc(_loc)
        {}

        norm_loc(base_loc _loc) noexcept
            : clang_context(_loc.get_compiler()), loc(_loc.raw())
        {}

        // Get the raw underlying SourceRange (but, you know, don't)
        [[nodiscard]]
        clang::SourceLocation raw() const noexcept  { return loc; }

        [[nodiscard]]
        bool is_valid() const noexcept              { return loc.isValid(); }

        [[nodiscard]]
        bool is_macro() const noexcept              { return loc.isMacroID(); }

        [[nodiscard]]
        explicit operator bool() const noexcept     { return is_valid(); }

        [[nodiscard]]
        std::string str() const noexcept            { return loc.printToString(src_manager); }

        // Downgrade to base_loc
        [[nodiscard]]
        explicit operator base_loc() const noexcept { return base_loc{ loc, compiler }; }

        [[nodiscard]]
        llvm::StringRef path() const noexcept       { return src_manager.getFilename(loc); }

        void advance(int off) noexcept              { loc = loc.getLocWithOffset(off); }

        // Beginning of the first token
        [[nodiscard]] base_loc tok_begin() const noexcept
        {
            return {
                clang::Lexer::GetBeginningOfToken(loc, src_manager, lang_opts),
                compiler
            };
        }

        // End of the last token
        [[nodiscard]] base_loc tok_end() const noexcept
        {
            auto end = clang::Lexer::getLocForEndOfToken(loc, 0, src_manager, lang_opts);
            if (end.isValid()) return { end, compiler };

            unsigned int len = clang::Lexer::MeasureTokenLength(loc, src_manager, lang_opts);
            return { loc.getLocWithOffset(static_cast<int>(len)), compiler };
        }

        // Get next token
        [[nodiscard]] std::optional<base_loc> tok_next() const noexcept
        {
            auto next_tok = clang::Lexer::findNextToken(loc, src_manager, lang_opts);
            if (!next_tok.has_value()) return std::nullopt;

            return base_loc { next_tok->getLocation(), compiler };
        }

        // Recursively remove macro wrappers at location
        void strip_macros() noexcept
        {
            while (is_valid() && is_macro() && src_manager.isMacroArgExpansion(loc))
                loc = src_manager.getImmediateSpellingLoc(loc);
        }
    };

    // File-based clang::SourceLocation, deliberately incompatible with base_loc
    struct file_loc : public norm_loc {
        // Construct from clang::SourceLocation obtained from LLVM callbacks
        file_loc(const clang::SourceLocation _loc, clang::CompilerInstance &compiler) noexcept
            : norm_loc(base_loc{_loc, compiler}.normalized()) {}

        // Construct from a Decl's location (defaults to end)
        file_loc(const clang::Decl *_decl, clang::CompilerInstance &compiler, bool use_begin_loc = false) noexcept
            : norm_loc(src_manager.getFileLoc(use_begin_loc ? _decl->getBeginLoc() : _decl->getEndLoc()), compiler)
        {}

        // Construct from base_loc
        explicit file_loc(base_loc _loc) noexcept
            : norm_loc(_loc.normalized()) {}

        // Produce a file_loc from decl's begin
        [[nodiscard]]
        static file_loc from_decl_begin(clang::Decl *_decl, clang::CompilerInstance &compiler) noexcept
        {
            return file_loc { _decl, compiler, /*use_begin_loc=*/true };
        }

        // Produce a file_loc from decl's end
        [[nodiscard]]
        static file_loc from_decl_end(clang::Decl *_decl, clang::CompilerInstance &compiler) noexcept
        {
            return file_loc { _decl, compiler, /*use_begin_loc=*/false };
        }

        // Produce a file_loc from decl's type spec location
        [[nodiscard]]
        static file_loc from_decl_type(clang::FunctionDecl *_decl, clang::CompilerInstance &compiler) noexcept
        {
            return file_loc { _decl->getTypeSpecStartLoc(), compiler };
        }

        void set(clang::FileID fid, unsigned int pos) noexcept
        {
            auto start = src_manager.getLocForStartOfFile(fid);
            loc = start.getLocWithOffset(static_cast<int>(pos));
        }

        // Decompose into FileID + file position pair
        [[nodiscard]]
        fileid_pos decompose() const noexcept
        {
            auto decomp = src_manager.getDecomposedLoc(loc);
            return fileid_pos{ decomp.first, decomp.second };
        }
    };

    // Spelling clang::SourceLocation, deliberately incompatible with base_loc
    struct spell_loc : public norm_loc {
        // Construct from clang::SourceLocation obtained from LLVM callbacks
        spell_loc(const clang::SourceLocation _loc, clang::CompilerInstance &compiler) noexcept
            : norm_loc(src_manager.getSpellingLoc(_loc), compiler) {}

        // Construct from clang::SourceLocation obtained from LLVM callbacks
        spell_loc(base_loc _loc) noexcept
            : norm_loc(src_manager.getSpellingLoc(_loc.normalized().raw()), _loc.get_compiler()) {}

        spell_loc(file_loc _loc) noexcept
            : norm_loc(src_manager.getSpellingLoc(_loc.raw()), _loc.get_compiler()) {}

        [[nodiscard]]
        unsigned int line() const noexcept      { return src_manager.getSpellingLineNumber(loc); }
        [[nodiscard]]
        unsigned int col() const noexcept       { return src_manager.getSpellingColumnNumber(loc); }
    };

    ///////////////////////////////////////////////////////////////////////////
    // Ranges (SourceRange wrappers)
    ///////////////////////////////////////////////////////////////////////////

    // Basic wrapper for clang::SourceRange
    struct source_range : public clang_context {
        base_loc    begin;
        base_loc    end;

        source_range(const clang::SourceRange _range, clang::CompilerInstance &compiler) noexcept
            : clang_context(compiler),
              begin{_range.getBegin(), compiler},
              end{_range.getEnd(), compiler} {}

        source_range(base_loc _begin, base_loc _end) noexcept
            : clang_context(_begin.get_compiler()), begin(_begin), end(_end) {}

        [[nodiscard]]
        bool is_valid() const noexcept          { return begin.is_valid() && end.is_valid(); }

        [[nodiscard]]
        explicit operator bool() const noexcept { return is_valid(); }

        [[nodiscard]]
        std::string str() const noexcept        { return std::format("{}..{}", begin.str(), end.str()); }

        [[nodiscard]]
        bool is_virtual() const noexcept        { return begin.is_virtual() || end.is_virtual(); }
    };

    // Normalized source range
    template<typename loc_t>
    requires std::constructible_from<loc_t, base_loc>
    struct norm_range : public clang_context {
        loc_t   begin;
        loc_t   end;

        norm_range(const source_range _range) noexcept
            : clang_context(_range.get_compiler()),
              begin(loc_t{_range.begin}),
              end(loc_t{_range.end}) {}

        [[nodiscard]]
        std::string str() const noexcept        { return std::format("{}..{}", begin.str(), end.str()); }

        // Get the begin token
        [[nodiscard]]
        base_loc tok_begin() const noexcept     { return begin.tok_begin(); }

        // Get the end token
        [[nodiscard]] base_loc tok_end() const noexcept
        {
            if (auto ret = end.tok_end(); ret.is_valid())   return ret;
            return static_cast<base_loc>(end);
        }

        // Reconstruct raw char range as clang::SourceRange for LLVM calls
        [[nodiscard]] clang::SourceRange as_source_range() const noexcept
        {
            return { begin.raw(), end.raw() };
        }

        // Reconstruct a token range (end inclusive) as clang::CharSourceRange for
        // LLVM calls like getSourceText()
        [[nodiscard]] clang::CharSourceRange as_token_range() const noexcept
        {
            return clang::CharSourceRange::getTokenRange(as_source_range());
        }

        // Reconstruct the half-open token char range as clang::CharSourceRange
        // that covers our tokens, mapped to file locations
        [[nodiscard]] clang::CharSourceRange as_token_char_range() const noexcept
        {
            return clang::Lexer::getAsCharRange(as_token_range(), src_manager, lang_opts);
        }

        // Reconstruct raw char range of half-open range of tokens as
        // clang::CharSourceRange
        [[nodiscard]] clang::CharSourceRange as_char_range() const noexcept
        {
            return clang::CharSourceRange::getCharRange(begin.raw(), end.raw());
        }

        using range_getter_t = clang::CharSourceRange(norm_range<loc_t>::*)() const noexcept;

        // Get source text from this range
        template<range_getter_t getter = &norm_range<loc_t>::as_token_range>
        [[nodiscard]] std::string source() const noexcept
        {
            return clang::Lexer::getSourceText(
                (this->*getter)(), src_manager, compiler.getLangOpts()
            ).str();
        }
    };

    // File range
    struct file_range : public clang_context {
        norm_range<file_loc>    range;
        clang::FileID           fid;
        unsigned int            begin;
        unsigned int            end;

        file_range(source_range _range) noexcept
            : clang_context(_range.get_compiler()), range{_range}
        {
            auto tok_begin = range.tok_begin();
            auto tok_end = range.tok_end();

            fileid_pos fbp = tok_begin.decompose();
            fileid_pos fep = tok_end.decompose();

            fid = fbp.fid;

            // Warn if tokens are in different files, use the tok_begin's
            // FileID anyway
            if (fbp.fid != fep.fid) { err("token range spans files"); fep = fbp; }

            // If the range is backwards, set end to beginning
            if (fbp.pos > fep.pos) {
                err("token range goes backwards: ", fbp.pos, " -> ", fep.pos);
                fep.pos = fbp.pos;
            }

            begin = fbp.pos;
            end = fep.pos;
        }

        // Extend [begin,end) to include a trailing "( ... )" group that immediately
        // follows the declarator end (skips whitespace/comments). No spelling lookups.
        file_range &extend_to_trailing_parens() noexcept
        {
            using namespace clang;

            if (!is_valid())                return *this;

            llvm::StringRef buf = src_manager.getBufferData(fid);
            const char *base = buf.data();

            // Raw lexer starting exactly at current end offset.
            Lexer L(src_manager.getLocForStartOfFile(fid), lang_opts,
                    base, base + end, base + buf.size());
            L.SetKeepWhitespaceMode(false);

            Token tok;
            L.LexFromRawLexer(tok);                 // skip ws/comments, read next token

            if (tok.isNot(tok::l_paren))    return *this;    // nothing to extend

            // Balance parentheses
            int depth = 1;
            SourceLocation lastEnd = tok.getEndLoc();

            while (depth > 0) {
                L.LexFromRawLexer(tok);
                if (tok.is(tok::eof)) break;
                lastEnd = tok.getEndLoc();
                if (tok.is(tok::l_paren))      ++depth;
                else if (tok.is(tok::r_paren)) --depth;
            }
            if (depth == 0) {
                SourceLocation e = Lexer::getLocForEndOfToken(lastEnd, 0, src_manager, lang_opts);
                auto [fidE, offE] = src_manager.getDecomposedLoc(e);
                if (fidE == fid && offE >= begin) {
                    range.end.set(fid, offE);
                    end = offE; // extend in-place
                    // range = norm_range<file_loc> { source_range { range.begin.tok_begin(), base_loc { e, compiler } } };
                    // range.end.
                }
            }

            return *this;
        }

        unsigned int width() const noexcept                 { return end - begin; }

        [[nodiscard]]
        bool is_valid() const noexcept                      { return fid.isValid() && begin <= end; }

        [[nodiscard]]
        explicit operator bool() const noexcept             { return is_valid(); }

        [[nodiscard]]
        std::string str() const noexcept                    { return range.str(); }

        template<norm_range<file_loc>::range_getter_t getter = &norm_range<file_loc>::as_token_range>
        [[nodiscard]]
        std::string source() const noexcept                 { return range.source<getter>(); }

        [[nodiscard]]
        bool contains(unsigned int off) const noexcept      { return begin <= off && off < end; }

        [[nodiscard]]
        bool contains(const file_loc &fl) const noexcept
        {
            fileid_pos fp = fl.decompose();
            return fid == fp.fid && begin <= fp.pos && fp.pos < end;
        }

        [[nodiscard]]
        bool same_file(const file_range &fr) const noexcept { return fid == fr.fid; }

        [[nodiscard]]
        static bool same_file(const file_range &a, const file_range &b) noexcept
        {
            return a.same_file(b);
        }
    };

    struct spell_range : public clang_context {
    private:
        norm_range<spell_loc>   range;
    public:
        spell_range(source_range _range)
            : clang_context(_range.get_compiler()),
              range(_range) {}

        [[nodiscard]]
        spell_loc begin() const noexcept    { return range.tok_begin(); }
        [[nodiscard]]
        spell_loc end() const noexcept      { return range.tok_end(); }

        [[nodiscard]]
        std::string str() const noexcept    { return range.str(); }

        [[nodiscard]]
        std::string source() const noexcept { return range.source(); }
    };

    struct file_range_less {
        [[nodiscard]]
        bool operator()(const file_range &a, const file_range &b) const noexcept
        {
            if (a.fid != b.fid) return a.fid.getHashValue() < b.fid.getHashValue();
            if (a.begin != b.begin) return a.begin < b.begin;
            return a.end < b.end;
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // Command line options
    ///////////////////////////////////////////////////////////////////////////

    struct options {
        llvm::StringRef             output_base;
        llvm::StringRef             src_root;
        std::vector<std::string>    exclude_dirs;
        std::vector<std::string>    backends;
        std::string                 base_url;
        bool                        verbose;

        static void split_comma_list(llvm::StringRef str, std::vector<std::string> &list)
        {
            std::ranges::for_each(
                str | std::views::split(std::string_view(",")), [&](const auto &dir) {
                    list.push_back(to_string(dir));
                }
            );
        }

        options() : output_base{""}, src_root{""}, base_url{BASE_URL} {}

        // Parse plugin's CLI arguments
        options(const std::vector<std::string> &args) : base_url{BASE_URL}
        {
            std::ranges::for_each(
                args, [this](llvm::StringRef arg) {
                    if      (arg.consume_front("output.base=")) output_base = arg;
                    else if (arg.consume_front("output.url="))  base_url = arg;
                    else if (arg.consume_front("src_root="))    src_root = arg;
                    else if (arg == "verbose")                  { clap::verbose = verbose = true; }
                    else if (arg.consume_front("exclude="))     split_comma_list(arg, exclude_dirs);
                    else if (arg.consume_front("backends="))    split_comma_list(arg, backends);
                }
            );
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // Stream cache/producer
    ///////////////////////////////////////////////////////////////////////////

    struct streamer {
    private:
        std::unordered_map<std::string, std::unique_ptr<llvm::raw_fd_ostream>> stream_map;
        options &options;

    public:
        llvm::StringRef src_path;
        llvm::StringRef rel_src_path;

        streamer(llvm::StringRef src_path, struct options &options)
            : options(options),
              src_path(src_path),
              rel_src_path(src_path.substr(options.src_root.size()))
        {}

        // Make an output stream to an output file at
        // <output>/<source base name>.<suffix>
        //
        // If output.base was not specified, <output> is the same directory
        // as the source file.
        // If <output> doesn't exist, it is created.
        // Return the output stream to the resulting file or an error if any
        // of the steps fail.
        //
        // XXX: untangle and factor out the path construction spaghetti logic
        using stream_result = llvm::ErrorOr<std::unique_ptr<llvm::raw_fd_ostream>>;
        stream_result make_output_stream(std::string_view suffix, std::string_view path_override = "")
        {
            llvm::SmallString<PATH_MAX> output_path = src_path;

            bool base_is_dir = false;
            if (!options.output_base.empty()) {
                llvm::SmallString<PATH_MAX> full_path = options.output_base;
                full_path.append(path_override.length() ? path_override : rel_src_path.str());

                llvm::sys::path::remove_filename(full_path);

                if (llvm::sys::fs::exists(full_path)) {
                    if (llvm::sys::fs::is_directory(full_path)) 
                        base_is_dir = true;
                } else/* if (!llvm::sys::path::has_filename(full_path) ||
                            llvm::sys::path::filename(full_path) == ".")*/ {
                    auto err = llvm::sys::fs::create_directories(full_path);
                    if (err) {
                        llvm::errs()
                            << "Can't create "
                            << full_path << ": "
                            << err.message() << "\n";
                        return err;
                    }

                    base_is_dir = true;
                }

                if (base_is_dir)
                    output_path = full_path;
            }

            if (base_is_dir) {
                auto file_name = path_override.length()
                    ? llvm::sys::path::filename(path_override)
                    : llvm::sys::path::filename(src_path);
                llvm::sys::path::append(output_path, file_name);
            }
            output_path.append(suffix);

            log("Writing to: ", output_path);

            std::error_code err;
            auto os = std::make_unique<llvm::raw_fd_ostream>(output_path, err);

            if (err) {
                llvm::errs() << "Failed to open output file: " << err.message() << "\n";
                return err;
            }

            return std::move(os);
        }

        llvm::ErrorOr<llvm::raw_fd_ostream *>
        stream_find_get(std::string_view path, std::string_view suffix)
        {
            std::string key{path};
            key.append(suffix);

            if (stream_map.contains(key))
                return stream_map[key].get();

            auto res = make_output_stream(suffix, path);
            if (std::error_code err = res.getError())
                return err;

            stream_map[key] = std::move(res.get());
            return stream_map[key].get();
        }
    };

    class core : protected no_copy_no_move {
    protected:
        clang::CompilerInstance   &compiler;
        clang::Preprocessor       &preproc;
        clang::ASTContext         &ast_ctx;
        clang::SourceManager      &src_manager;
        std::unique_ptr<options>  options;
        std::unique_ptr<streamer> streamer;

        // Path of the main TU relative to the root of the source tree (src_root)
        llvm::StringRef     rel_src_path;

    public:
        core(clang::CompilerInstance &ci, std::unique_ptr<struct options> _options)
            : compiler(ci),
              preproc(ci.getPreprocessor()),
              ast_ctx(ci.getASTContext()),
              src_manager(ci.getSourceManager()),
              options(std::move(_options))
        {
            auto file_id = src_manager.getMainFileID();
            auto src_path = src_manager.getFileEntryForID(file_id)->tryGetRealPathName();

            streamer = std::make_unique<struct streamer>(src_path, *options.get());
            rel_src_path = streamer->rel_src_path;
        }

        struct options &get_options() const { return *options.get(); }

        // Test absolute path for being in the source tree
        template<typename str_t>
        bool is_source_path(const str_t &path) const
        {
            if (!llvm::sys::path::is_absolute(path)) return true;
            return path.starts_with(options->src_root);
        }

        template<typename str_t>
        bool is_excluded_path(str_t &path) const
        {
            // Skip system/3rd party header files (TODO: an option?)
            if (!is_source_path(path))   return true;

            return std::ranges::any_of(
                options->exclude_dirs,
                [this, &path](const auto &dir) {
                    std::string abs_dir = options->src_root.str();
                    abs_dir.append(dir);
                    return path.starts_with(dir) || path.starts_with(abs_dir);
                }
            );
        }

        template<typename str_t>
        str_t get_rel_path(const str_t &path)
        {
            if (!is_source_path(path))  return path;
            return path.substr(options->src_root.size());
        }

        std::string_view get_rel_path_view(std::string &path)
        {
            if (!is_source_path(path))  return path;
            std::string_view ret{path.substr(options->src_root.size())};
            return ret;
        }
    };
} // namespace clap::plugin

#endif // __CLAP_PLUGIN_HPP__
