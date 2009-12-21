#pragma once

#include <iostream>
#include <vector>

#include "Macros.h"
#include "Expr.h"
#include "IExprVisitor.h"
#include "Ref.h"
#include "String.h"

namespace Finch
{
    using std::vector;
    
    // AST node for a block: "{|param| obj message }"
    class BlockExpr : public Expr
    {
    public:
        BlockExpr(vector<String> params, Ref<Expr> body)
        :   mParams(params),
            mBody(body)
        {}
        
        vector<String>  Params() const { return mParams; }
        Ref<Expr>       Body() const { return mBody; }
        
        virtual void Trace(std::ostream & stream) const
        {
            stream << "{";
            
            if (mParams.size() > 0)
            {
                stream << "|";
                
                for (int i = 0; i < mParams.size(); i++)
                {
                    stream << mParams[i];
                    
                    if(i < mParams.size() - 1) stream << " ";
                }            
                
                stream << "|";
            }
            
            stream << " " << mBody << " }";
        }
            
        EXPRESSION_VISITOR

    private:
        vector<String> mParams;
        Ref<Expr>      mBody;
    };
}