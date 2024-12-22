#include "processor_test_pattern_match.h"
#include "daq.h"
#include "utils.h"

namespace apdcam10g
{
    void processor_test_pattern_match::init()
    {
        next_data_ = 0;
        summaries_.clear();
        summaries_.resize(daq_->all_enabled_channels_.size());
        offsets_.clear();
        offsets_.resize(daq_->all_enabled_channels_.size(),0);
        offset_set_.clear();
        offset_set_.resize(daq_->all_enabled_channels_.size(),false);
        n_received_shots_.clear();
        n_received_shots_.resize(daq_->all_enabled_channels_.size(),0);
        n_missing_shots_.clear();
        n_missing_shots_.resize(daq_->all_enabled_channels_.size(),0);
    }

    size_t processor_test_pattern_match::run_unique_(size_t from, size_t to)
    {
        for(size_t i_shot=from; i_shot<to; ++i_shot)
        {
            for(unsigned int i_enabled_channel=0; i_enabled_channel<daq_->all_enabled_channels_.size(); ++i_enabled_channel)
            {
                ++n_received_shots_[i_enabled_channel];
                auto *c = daq_->all_enabled_channels_[i_enabled_channel];
                if(!offset_set_[i_enabled_channel])
                {
                    for(int offs = 0; offs<sequence_->size(); ++offs)
                    {
                        if((*c)(i_shot) == (*sequence_)[(i_shot+offs)%sequence_->size()])
                        {
                            offsets_[i_enabled_channel] = offs;
                            offset_set_[i_enabled_channel] = true;
                            break;
                        }
                    }
                    if(!offset_set_[i_enabled_channel])
                    {
                        summaries_[i_enabled_channel] += "[" + std::to_string(i_shot) + "] NO MATCH\n";
                    }
                }
                else
                {
                    // We only record the problems: missing samples
                    if((*c)(i_shot) != (*sequence_)[(i_shot+offsets_[i_enabled_channel])%sequence_->size()])
                    {
                        offset_set_[i_enabled_channel] = false;
                        //bool found = false;
                        for(size_t missing=1; missing<sequence_->size(); ++missing)
                        {
                            if((*c)(i_shot) == (*sequence_)[(i_shot+offsets_[i_enabled_channel]+missing)%sequence_->size()])
                            {
                                offset_set_[i_enabled_channel] = true;
                                offsets_[i_enabled_channel] = (offsets_[i_enabled_channel]+missing)%sequence_->size();
                                summaries_[i_enabled_channel] += "[" + std::to_string(i_shot) + "] missing " + std::to_string(missing) + "\n";
                                n_missing_shots_[i_enabled_channel] += missing;
                                break;
                            }
                        }
                        if(!offset_set_[i_enabled_channel])
                        {
                            summaries_[i_enabled_channel] += "[" + std::to_string(i_shot) + "] could not re-match\n";
                        }
                    }
                }
            }
        }
        return (next_data_=to);
    }

    size_t processor_test_pattern_match::run(size_t from_counter, size_t to_counter)
    {
        if(sequence_==0) APDCAM_ERROR("No sequence is given in processor_test_pattern_match");
        
        const size_t start = std::max(from_counter, next_data_);
        if(sequence_->unique_elements()) return run_unique_(start,to_counter);

        APDCAM_ERROR("Can not handle yet a test pattern sequence with non-unique elements");
    }

    void processor_test_pattern_match::finish()
    {
        output_lock lck;
        cerr<<daq::section_start("TEST PATTERN MATCH")<<endl;
        unsigned int problems = 0;
        for(unsigned int i_enabled_channel=0; i_enabled_channel<daq_->all_enabled_channels_.size(); ++i_enabled_channel)
        {
            if(summaries_[i_enabled_channel] != "")
            {
                auto *c = daq_->all_enabled_channels_[i_enabled_channel];
                cerr<<"Channel "<<c->board_number<<"/"<<c->channel_number<<endl;
                cerr<<summaries_[i_enabled_channel];
                cerr<<"Received shots: "<<n_received_shots_[i_enabled_channel]<<endl;
                cerr<<"Missing shots : "<<n_missing_shots_[i_enabled_channel]<<endl;
                cerr<<endl;
                ++problems;
            }
        }
        if(problems==0) cerr<<"No problems"<<endl;
        else cerr<<"There were missing shots in "<<problems<<" channels"<<endl;
        cerr<<daq::section_start("TEST PATTERN MATCH")<<endl;
    }
    
}
