import daq

class ProcessorTest:

    def __init__(self):

        self.next_ = 0;

    def run(self,buffers,from_counter,to_counter):
    
        # Even if data is (still) available in the ring buffers starting from counter 'from_counter',
        # we only want to analyze from 'next' because we analyzed all previous shots so far
        if self.next_ > from_counter:
            from_counter = self.next_

        # loop over all shots
        for i_shot in range(from_counter,to_counter):
            # loop over all channels
            for i_channel in range(128):
                # Take action only for the allowed channels
                if buffers[i_channel] is not None:
                    # The first index in [i_channel] selects the channel. Subsequent values of
                    # this channel can be accessed via the [] operator using the counter i_shot
                    #print("Channel #" + str(i_channel) + " value: " + str(buffers[i_channel][i_shot]))
                    buffers[i_channel][i_shot] += 10

        # set the shot counter that we need to analyze next. 
        self.next_ = to_counter

        # Return the earliest shot number that we request to stay in the buffer. We analyze
        # shot-by-shot (that is, not in a sliding window), so we return the end of the available
        # range, so that all shots can be removed from the buffer by the DAQ C++ process loop.
        # Other analysis tasks may request earlier shots to stay in the buffer...
        return self.next_
