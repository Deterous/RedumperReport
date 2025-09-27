module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <string>
#include <vector>
#include "redumper/utils/throw_line.hh"

export module cd.common;

import cd.cd;
import cd.toc;
import utils.strings;

namespace gpsxre
{

export const int32_t LBA_START = -45150; // MSF_TO_LBA(MSF_LEADIN_START);

export int32_t lba_to_sample(int32_t lba, int32_t offset)
{
    return lba * CD_DATA_SIZE_SAMPLES + offset;
}

export std::list<std::string> cue_get_entries(const std::filesystem::path &cue_path)
{
    std::list<std::string> entries;

    std::fstream fs(cue_path, std::fstream::in);
    if(!fs.is_open())
        throw_line("unable to open file ({})", cue_path.filename().string());

    std::string entry;
    std::string line;
    while(std::getline(fs, line))
    {
        auto tokens(tokenize(line, " \t\r", "\"\""));
        if(tokens.size() == 3)
        {
            if(tokens[0] == "FILE")
                entry = tokens[1];
            else if(tokens[0] == "TRACK" && !entry.empty())
            {
                entries.push_back(entry);
                entry.clear();
            }
        }
    }

    return entries;
}

export TOC get_toc(const std::filesystem::path &toc_path)
{
    std::ifstream toc_fs(toc_path, std::ios::in | std::ios::binary);
    std::vector<uint8_t> toc_buffer((std::istreambuf_iterator<char>(toc_fs)), {});
    TOC toc(toc_buffer, false);

    return toc;
}

export TOC get_fulltoc(const std::filesystem::path &fulltoc_path, TOC &toc)
{
    std::ifstream fulltoc_fs(fulltoc_path, std::ios::in | std::ios::binary);
    std::vector<uint8_t> fulltoc_buffer(std::istreambuf_iterator<char>(fulltoc_fs), {});
    TOC fulltoc(fulltoc_buffer, true);
    fulltoc.deriveINDEX(toc);

    return fulltoc;
}

}