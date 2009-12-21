#pragma once

#include "Macros.h"
#include "Parser.h"
#include "Expr.h"
#include "Ref.h"

namespace Finch
{
    class ILineReader;
    
    // Parser for the Finch grammar.
    class FinchParser : private Parser
    {
    public:
        FinchParser(ITokenSource * tokens)
        :   Parser(tokens)
        {}
        
        // Parses a full file of source code. Will read as many lines as are
        // available until the source is done.
        // Returns a null reference if parsing failed.
        Ref<Expr> ParseFile();
        
        // Parses a single line. Will stop once a complete terminated expression
        // is reached.
        // Returns a null reference if parsing failed.
        Ref<Expr> ParseLine();

    private:
        Ref<Expr> Expression();
        Ref<Expr> Sequence();
        Ref<Expr> Variable();
        Ref<Expr> Keyword();
        Ref<Expr> Operator();
        Ref<Expr> Unary();
        Ref<Expr> Primary();
        
        Ref<Expr> KeywordMessage(Ref<Expr> object);
        
        Ref<Expr> ParseError();
        Ref<Expr> ParseError(const char * message);
        
        NO_COPY(FinchParser)
    };
}