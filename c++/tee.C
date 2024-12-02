#include "tee.h"

namespace apdcam10g
{
    ostream &tee(ostream &orig_stream, const std::filesystem::path &filename, std::ios_base::openmode mode)
    {
        std::streambuf *b1 = orig_stream.rdbuf();
        std::filebuf   *b2 = new std::filebuf;
        b2->open(filename,mode);
        orig_stream.rdbuf(new teebuf(b1,b2,false,true));
        return orig_stream;
    }
    
}
