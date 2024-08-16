#include <thread>
#include <string>
#include <string.h>
#include <iostream>
#include <bit>
#include <stdint.h>
#include <tuple>
#include <deque>
//#include "error.h"
#include "ring_buffer.h"
//#include "safe_semaphore.h"
#include <string.h>
using namespace std;
using namespace apdcam10g;




int main()
{

    ring_buffer<char> buffer(10);

    std::jthread consumer([&]{
        while(true)
        {
            buffer.wait_for_size();
            char c = buffer.front();
            if(c=='\r') break;
            cerr<<c;
            buffer.pop_front();
        }
    });

    std::jthread producer([&]{
        std::string text = "Hol volt, hol nem volt, volt egyszer egy kislany, aki elment vadaszni. Az erdoben talalkozott a nagy buidos farkassal. Hogy vagy kislany? Kredezte a farkas. Csak nem vadaszol? Azzal bekapta";
        for(int i=0; i<text.size(); ++i)
        {
            buffer.wait_for_space();
            buffer.push_back(text[i]);
        }
        buffer.push_back('\r');
    });

    producer.join();
    consumer.join();

    return 0;
}
