#include "StringObject.h"

namespace Finch
{
    void StringObject::Trace(std::ostream & stream) const
    {
        stream << "\"" << mValue << "\"";
    }
    
    Ref<Object> StringObject::Receive(String message, vector<Ref<Object> > args)
    {
        //### bob: should do some sort of message not handled thing here
        return Ref<Object>();
    }
}