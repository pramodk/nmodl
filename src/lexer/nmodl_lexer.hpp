#ifndef NMODL_LEXER_HPP
#define NMODL_LEXER_HPP

#include "ast/ast.hpp"
#include "parser/nmodl_parser.hpp"

/** Flex expects the declaration of yylex to be defined in the macro YY_DECL
 * and C++ parser class expects it to be declared. */
#ifndef YY_DECL
#define YY_DECL nmodl::Parser::symbol_type nmodl::Lexer::next_token()
#endif

/** For creating multiple (different) lexer classes, we can use '-P' flag
 * (or prefix option) to rename each yyFlexLexer to some other name like
 * ‘xxFlexLexer’. And then include <FlexLexer.h> in other sources once per
 * lexer class, first renaming yyFlexLexer as shown below. */
#ifndef __FLEX_LEXER_H
#define yyFlexLexer NmodlFlexLexer
#include "FlexLexer.h"
#endif

namespace nmodl {

    /**
     * \class Lexer
     * \brief Represent Lexer/Scanner class for NMODL language parsing
     *
     * Lexer defined to add some extra function to the scanner class from flex.
     * Flex itself creates yyFlexLexer class, which we renamed using macros to
     * NmodlFlexLexer. But we change the context of the generated yylex() function
     * because the yylex() defined in NmodlFlexLexer has no parameters. Also, note
     * that implementation of the member functions are in nmodl.l file due to use
     * of macros. */
    class Lexer : public NmodlFlexLexer {
      public:
        /** Reference to driver object which contains this lexer instance. This is
         * used for error reporting and checking macro definitions. */
        Driver& driver;

        /// For tracking location of the tokens
        location loc;

        /// Units are stored in the scanner (could be stored in driver)
        ast::String* last_unit = nullptr;

        /** For reaction ('~') we return different token based on the lexical
         * context, store the current context. Note that this was returned from
         * parser in original implementation. */
        int lexcontext = 0;

        /** The streams in and out default to cin and cout, but that assignment
         * is only made when initializing in yylex(). */
        explicit Lexer(Driver& drv, std::istream* in = nullptr, std::ostream* out = nullptr)
            : driver(drv), NmodlFlexLexer(in, out) {}

        ~Lexer() override= default;;

        /** Main lexing function generated by flex according to the macro declaration
         * YY_DECL above. The generated bison parser then calls this virtual function
         * to fetch new tokens. Note that yylex() has different declaration and hence
         * it can't be used for new lexer. */
        virtual Parser::symbol_type next_token();

        /// Enable debug output (via yyout) if compiled into the scanner.
        void set_debug(bool b);

        /** For units we have to consume string until end of closing parenthesis
         * and store it in the scanner object. */
        void scan_unit();

        /** Return unit object created by scan_unit(). Initialize to nullptr to avoid
         * using empty units or units from last parsing. */
        ast::String* get_unit();

        /// For TITLE we have to input string until end of line.
        std::string inputline();

        /** Due to copy more the end position is not accurate. Set column 0 to avoid
         * confusion (see NOCMODL-25). */
        void reset_end_position() {
            loc.end.column = 0;
        }
    };

}    // namespace nmodl

#endif