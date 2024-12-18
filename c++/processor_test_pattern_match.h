#ifndef __APDCAM10G_PROCESSOR_TEST_PATTERN_MATCH_H__
#define __APDCAM10G_PROCESSOR_TEST_PATTERN_MATCH_H__

#include "processor.h"
#include "test_pattern.h"

namespace apdcam10g
{
    class processor_test_pattern_match : public processor
    {
    private:
        test_pattern_sequence *sequence_;

        // The shot counter pointing to the next shot that has not yet been written to disk
        size_t next_data_ = 0;
        
        size_t run_unique_(size_t from, size_t to);

        std::vector<std::string> summaries_;
        std::vector<int> offsets_;
        std::vector<bool> offset_set_;
        std::vector<unsigned int> n_received_shots_, n_missing_shots_;

    public:
        processor_test_pattern_match(test_pattern_sequence *s=0) : sequence_(s) {}
        void sequence(test_pattern_sequence *s) { sequence_ = s; }
        void init() override;
        size_t run(size_t from_counter, size_t to_counter) override;
        void finish() override;
    };

}

#endif
