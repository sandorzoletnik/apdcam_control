#ifndef __APDCAM10G_CONFIG_H__
#define __APDCAM10G_CONFIG_H__

#include <string>

namespace apdcam10g
{
  namespace config
  {
      const unsigned int max_boards = 4;
      const unsigned int chips_per_board = 4;
      const unsigned int channels_per_chip = 8;
      const unsigned int channels_per_board = chips_per_board*channels_per_chip;
      const unsigned int max_channels = max_boards*channels_per_board;
      const unsigned int ports[4] = {10000, 10001, 10002, 10003};
      const std::string configdir = ".apdcam10g";
  }
}

#endif
