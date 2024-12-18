#ifndef __APDCAM10G_TEST_PATTERN_H__
#define __APDCAM10G_TEST_PATTERN_H__

#include <vector>
#include <cstdint>
#include <limits>
#include "error.h"
#include "typedefs.h"

namespace apdcam10g
{

    // A sequence (std::vector) of data_type variables representing a test pattern sequence, that the APDCAM is sending
    // to the DAQ. It is a purely virtual base class, different test patterns must be derived, and
    // - override the generate() function, using the value 'bits_' derived from this base class
    // - call generate() in their constructor
    class test_pattern_sequence : public std::vector<apdcam10g::data_type>
    {
    protected:
        // The number of bits in the ADC sample
        unsigned int bits_ = 14;
        virtual void generate_() = 0;
    public:
        // Set the number of bits of the values in the pseudo-random number sequence
        virtual void bits(unsigned int b) { bits_ = b; generate_(); }

        // This function must be implemented in all derived classes. It should return true if all elements of
        // the sequence are unique, i.e. they do not occur more than once.
        virtual bool unique_elements() const = 0;
    };

    class pseudo_random_short : public test_pattern_sequence
    {
    protected:
        void generate_() override;
    public:
        pseudo_random_short() { generate_(); }
        void bits(unsigned int b) override { if(b!=14) APDCAM_ERROR("pseudo_random_short can only generate 14 bit samples"); test_pattern_sequence::bits(b); }
        bool unique_elements() const override { return true; }
    };

    class test_pattern_generator
    {
    private:
        test_pattern_sequence *sequence_ = 0;
        unsigned int index_ = 0;
    public:
        test_pattern_generator(test_pattern_sequence *s=0) : sequence_(s) {}

        void sequence(test_pattern_sequence *s) 
            { 
                sequence_ = s; 
                index_ = 0;
            }

        // Return the next value from the test pattern sequence
        apdcam10g::data_type get() 
            { 
                if(!sequence_)
                {
                    unsigned int m = std::numeric_limits<data_type>::max();
                    ++m;
                    return (index_++)%m;
                    // This can overflow, in fact the bit resolution should be checked and used!
                }
                return (*sequence_)[(index_++)%sequence_->size()]; 
            }

        // Set the current index in the pseudo number sequence. It is affecting the results of the call
        // to get()
        void index(unsigned int i) { index_ = i; }

        // Randomize the index
        void random_index();
    };

}

#endif
