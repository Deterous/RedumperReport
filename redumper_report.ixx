module;

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <list>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "redumper/utils/throw_line.hh"

export module redumper_report;

import cd.cd;
import cd.cdrom;
import cd.common;
import cd.toc;
import common;
import dvd.common;
import filesystem.iso9660;
import readers.data_reader;
import readers.image_bin_reader;
import readers.image_iso_reader;
import scsi.mmc;
import utils.file_io;
import utils.logger;
import utils.misc;



namespace gpsxre
{


void print_sector_info(uint8_t *file_data, uint32_t sector_number, bool iso, uint32_t &prev_mode, Sector::SubHeader &prev_subheader)
{
    if(iso)
    {
        // check if all zeros
        if(!std::all_of(file_data, file_data + FORM1_DATA_SIZE, [](int x) { return x == 0; }))
        {
            LOG("Non-zero data in sector {}", sector_number);
        }
    }
    else
    {
        auto sector = reinterpret_cast<Sector*>(file_data);

        // check sync
        if(memcmp(sector, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
            LOG("Sector {}: Unexpected sync pattern", sector_number);

        if(sector->header.mode == 1)
        {
            // check sector mode has not changed
            if(prev_mode != 1)
                LOG("Sector {} onwards: Mode 1", sector_number);

            // check intermediate is zeroed
            if(!std::all_of(sector->mode1.intermediate, sector->mode1.intermediate + 8, [](uint8_t x){ return x == 0; }))
            {
                LOG("Sector {}: Non-zero Mode 1 Intermediate:", sector_number);
            }
            prev_mode = 1;
        }
        else if(sector->header.mode == 2)
        {
            if(sector->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
            {
                // check sector mode has not changed
                if(prev_mode != 3)
                    LOG("Sector {} onwards: Mode 2 Form 2", sector_number);
                prev_mode = 3;
            }
            else
            {
                // check sector mode has not changed
                if(prev_mode != 2)
                    LOG("Sector {} onwards: Mode 2 Form 1", sector_number);
                prev_mode = 2;
            }

            // check subheader changes
            if(sector_number > 0)
            {
                if(sector->mode2.xa.sub_header.file_number != prev_subheader.file_number)
                    LOG("Sector {} onwards: Subheader file number changed: {} -> {}", sector_number, prev_subheader.file_number, sector->mode2.xa.sub_header.file_number);
                if(sector->mode2.xa.sub_header.channel != prev_subheader.channel)
                    LOG("Sector {} onwards: Subheader channel changed: {} -> {}", sector_number, prev_subheader.channel, sector->mode2.xa.sub_header.channel);
                if(sector->mode2.xa.sub_header.submode != prev_subheader.submode)
                    LOG("Sector {} onwards: Subheader submode changed: {} -> {}", sector_number, prev_subheader.submode, sector->mode2.xa.sub_header.submode);
                if(sector->mode2.xa.sub_header.coding_info != prev_subheader.coding_info)
                    LOG("Sector {} onwards: Subheader coding info changed: {} -> {}", sector_number, prev_subheader.coding_info, sector->mode2.xa.sub_header.coding_info);
            }

            // check subheader copy matches
            if(sector->mode2.xa.sub_header.file_number != sector->mode2.xa.sub_header_copy.file_number)
                LOG("Sector {}: Subheader file number mismatch", sector_number);
            if(sector->mode2.xa.sub_header.channel != sector->mode2.xa.sub_header_copy.channel)
                LOG("Sector {}: Subheader channel mismatch", sector_number);
            if(sector->mode2.xa.sub_header.submode != sector->mode2.xa.sub_header_copy.submode)
                LOG("Sector {}: Subheader submode mismatch", sector_number);
            if(sector->mode2.xa.sub_header.coding_info != sector->mode2.xa.sub_header_copy.coding_info)
                LOG("Sector {}: Subheader coding info mismatch", sector_number);

            prev_subheader = sector->mode2.xa.sub_header;
        }
    }
}


void print_skeleton_info(std::filesystem::path skeleton_path, uint32_t sectors_count, bool iso)
{
    std::ifstream skeleton(skeleton_path, std::ifstream::in | std::ifstream::binary);
    if(!skeleton.is_open())
    {
        LOG("failed to open: {}", skeleton_path.string());
        return;
    }

    std::vector<uint8_t> sector(iso ? FORM1_DATA_SIZE : CD_DATA_SIZE);
    uint32_t prev_mode = 0;
    Sector::SubHeader prev_subheader;
    for(uint32_t lba = 0; lba < sectors_count; lba++)
    {
        skeleton.read((char *)sector.data(), sector.size());
        if(skeleton.fail())
        {
            LOG("read failed: {}", skeleton_path.string());
            return;
        }
        print_sector_info(sector.data(), lba, iso, prev_mode, prev_subheader);
    }
}


void print_iso_fs(std::filesystem::path skeleton, uint32_t sectors_count, bool iso)
{
    std::unique_ptr<DataReader> data_reader;
    if(iso)
        data_reader = std::make_unique<Image_ISO_Reader>(skeleton.string());
    else
        data_reader = std::make_unique<Image_BIN_Reader>(skeleton.string());

    auto area_map = iso9660::area_map(data_reader.get(), sectors_count);
    if(area_map.empty())
    {
        LOG("No ISO9660 content found");
        return;
    }

    LOG("ISO9660 map: ");
    for(auto const &area : area_map)
    {
        auto count = scale_up(area.size, FORM1_DATA_SIZE);
        LOG("LBA: [{:7} .. {:7}), count: {:6}, size: {:10}, type: {}{}", area.lba, area.lba + count, count, area.size, iso9660::area_type_to_string(area.type),
            area.name.empty() ? "" : std::format(", name: {}", area.name));
    }

    return;
}


export int redumper_report(std::filesystem::path file, bool verbose)
{
    int exit_code = 0;

    auto report_file = file;
    Logger::get().setFile(report_file.replace_extension(".report.txt"));

    LOG("");
    LOG("Redumper Log Report for: {}", file.stem().string());

    bool dvd_detected = false; // if .manufacturer exists (DVD), .physical must exist
    bool cd_detected = true; // if .physical doesn't exist (CD), .cue/.toc/etc must exist

    // manufacturer file(s)
    auto manufacturer_file = file.replace_extension(".manufacturer");
    auto temp_path = manufacturer_file;
    if(std::filesystem::exists(manufacturer_file))
    {
        cd_detected = false;
        dvd_detected = true;
        LOG("");
        LOG("*** found: {}", manufacturer_file.string());
        auto dmi = read_vector(manufacturer_file);
        if(dmi.size() != 2052)
            LOG("warning: unexpected size");
        if(dmi.size() > 4)
        {
            if(!(dmi[0] == 0x08 && dmi[1] == 0x02 && dmi[2] == 0x00 && dmi[3] == 0x00))
                LOG("warning: unexpected SCSI header");
            if(std::all_of(dmi.begin() + 4, dmi.end(), [](int x) { return x == 0; }))
                LOG("Zeroed DMI");
            else
                LOG("Non-zero data in DMI");
        }
    }
    else if(std::filesystem::exists(temp_path.replace_extension(".0.manufacturer")))
    {
        cd_detected = false;
        dvd_detected = true;
        LOG("");
        LOG("*** found: {}", temp_path.string());
        auto dmi = read_vector(temp_path);
        if(dmi.size() != 2052)
            LOG("warning: unexpected size");
        if(dmi.size() > 4)
        {
            if(!(dmi[0] == 0x08 && dmi[1] == 0x02 && dmi[2] == 0x00 && dmi[3] == 0x00))
                LOG("warning: unexpected SCSI header");
            if(std::all_of(dmi.begin() + 4, dmi.end(), [](int x) { return x == 0; }))
                LOG("Zeroed DMI");
            else
                LOG("Non-zero data in DMI");
        }
        temp_path = manufacturer_file;
        for(uint32_t structures = 1; std::filesystem::exists(temp_path.replace_extension(std::format(".{}.manufacturer", structures))); structures++)
        {
            LOG("");
            LOG("*** found: {}", temp_path.string());
            auto dmi = read_vector(temp_path);
            if(dmi.size() != 2052)
                LOG("warning: unexpected size");
            if(dmi.size() > 4)
            {
                if(!(dmi[0] == 0x08 && dmi[1] == 0x02 && dmi[2] == 0x00 && dmi[3] == 0x00))
                    LOG("warning: unexpected SCSI header");
                if(std::all_of(dmi.begin() + 4, dmi.end(), [](int x) { return x == 0; }))
                    LOG("Zeroed DMI");
                else
                    LOG("Non-zero data in DMI");
            }
            temp_path = manufacturer_file;
        }
    }

    // physical file(s)
    auto physical_file = file.replace_extension(".physical");
    temp_path = physical_file;
    if(std::filesystem::exists(physical_file))
    {
        cd_detected = false;
        LOG("");
        LOG("*** found: {}", physical_file.string());
        auto pfi = read_vector(physical_file);
        auto &pfi_layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)pfi[sizeof(CMD_ParameterListHeader)];
        if(dvd_detected)
        {
            LOG("info: .manufacturer found, assuming DVD");
            LOG("DVD PFI structure:");
            print_physical_structure(pfi_layer_descriptor, 0);
        }
        else
        {
            LOG("info: .manufacturer not found, assuming Blu-ray");
            LOG("Blu-ray PIC structure:");
            print_di_units_structure(&pfi[sizeof(CMD_ParameterListHeader)], true);
        }
    }
    else if(std::filesystem::exists(temp_path.replace_extension(".0.physical")))
    {
        cd_detected = false;
        LOG("");
        LOG("*** found: {}", temp_path.string());
        auto pfi = read_vector(physical_file);
        auto &pfi_layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)pfi[sizeof(CMD_ParameterListHeader)];
        if(dvd_detected)
        {
            LOG("info: .manufacturer found, assuming DVD");
            LOG("DVD PFI structure:");
            print_physical_structure(pfi_layer_descriptor, 0);
        }
        else
        {
            LOG("info: .manufacturer not found, assuming Blu-ray");
            LOG("Blu-ray PIC structure:");
            print_di_units_structure(&pfi[sizeof(CMD_ParameterListHeader)], true);
        }

        temp_path = physical_file;
        for(uint32_t structures = 1; std::filesystem::exists(temp_path.replace_extension(std::format(".{}.physical", structures))); structures++)
        {
            temp_path = physical_file;
            LOG("*** found: {}", temp_path.string());
            if(dvd_detected)
            {
                LOG("info: .manufacturer found, assuming DVD");
                LOG("DVD PFI structure:");
                print_physical_structure(pfi_layer_descriptor, 0);
            }
            else
            {
                LOG("info: .manufacturer not found, assuming Blu-ray");
                LOG("Blu-ray PIC structure:");
                print_di_units_structure(&pfi[sizeof(CMD_ParameterListHeader)], true);
            }
        }
    }
    else if(dvd_detected)
    {
        LOG("");
        LOG("*** warning: physical file not found: {}", physical_file.string());
    }

    // cue file
    auto cue_file = file.replace_extension(".cue");
    std::list<std::string> track_names;
    if(std::filesystem::exists(cue_file))
    {
        LOG("");
        LOG("*** found: {}", cue_file.string());
        uint32_t track_num = 1;
        track_names = cue_get_entries(cue_file);
        if(verbose)
        {
            for(auto &t : track_names)
            {
                LOG("Track {}: {}", track_num, t);
                track_num++;
            }
        }
    }
    else if(cd_detected)
    {
        LOG("");
        LOG("*** warning: cue file not found: {}", cue_file.string());
    }

    // toc and fulltoc files
    auto toc_file = file.replace_extension(".toc");
    auto fulltoc_file = file.replace_extension(".fulltoc");
    if(std::filesystem::exists(fulltoc_file) && std::filesystem::exists(toc_file))
    {
        LOG("");
        LOG("*** found: {}", toc_file.string());
        TOC toc = get_toc(toc_file);
        if(verbose)
        {
            std::stringstream ss;
            toc.print(ss);
            std::string line;
            while(std::getline(ss, line))
                LOG("{}", line);
        }

        LOG("");
        LOG("*** found: {}", fulltoc_file.string());
        TOC fulltoc = get_fulltoc(fulltoc_file, toc);
        if(verbose)
        {
            std::stringstream ss_full;
            fulltoc.print(ss_full);
            std::string line;
            while(std::getline(ss_full, line))
                LOG("{}", line);
        }
    }
    else if(std::filesystem::exists(toc_file))
    {
        LOG("");
        LOG("*** found: {}", toc_file.string());
        TOC toc = get_toc(toc_file);

        std::stringstream ss;
        toc.print(ss);
        std::string line;
        while(std::getline(ss, line))
            LOG("{}", line);
    }
    else if(cd_detected)
    {
        LOG("");
        LOG("*** warning: toc file not found: {}", cue_file.string());
    }

    // subcode file
    auto subcode_file = file.replace_extension(".subcode");
    if(std::filesystem::exists(subcode_file))
    {
        LOG("");
        LOG("*** found: {}", subcode_file.string());
    }
    else if(cd_detected)
    {
        LOG("");
        LOG("*** warning: subcode file not found: {}", cue_file.string());
    }

    // atip file
    auto atip_file = file.replace_extension(".atip");
    if(std::filesystem::exists(atip_file))
    {
        LOG("");
        LOG("*** found: {}", atip_file.string());
    }

    // pma file
    auto pma_file = file.replace_extension(".pma");
    if(std::filesystem::exists(pma_file))
    {
        LOG("");
        LOG("*** found: {}", pma_file.string());
    }

    // cdtext file
    auto cdtext_file = file.replace_extension(".cdtext");
    if(std::filesystem::exists(cdtext_file))
    {
        LOG("");
        LOG("*** found: {}", cdtext_file.string());
    }

    // log file
    auto log_file = file.replace_extension(".log");
    if(std::filesystem::exists(log_file))
    {
        LOG("");
        LOG("*** found: {}", log_file.string());

        if(track_names.empty())
        {
            // read CUE from log file
            uint32_t track_num = 1;
            for(auto &t : track_names)
            {
                LOG("Track {}: {}", track_num, t);
                track_num++;
            }
        }
    }
    else
    {
        LOG("");
        LOG("*** warning: log file not found: {}", log_file.string());
    }

    // state file
    // for DVD, 1 byte = 1 sector, byte 0 = LBA 0
    // for CD, 1 byte = 1 sample (4 bytes on disc)
    // entry (per sector) size of CD_SIZE/SAMPLE_SIZE = 2352/4 (588 bytes)
    // state file starts with 45150 sectors (10min) prepended zeroes (45150*588 = 26548200 bytes)
    // state file is offset by subtracting by drive read offset (i.e. an offset of -12 is placed 12 bytes to the right)
    // i.e. when writing state for sector X, 588 bytes would be placed at X*588 - read_offset until (X+1)*588 - read_offset - 1
    auto state_file = file.replace_extension(".state");
    if(std::filesystem::exists(state_file))
    {
        LOG("");
        LOG("*** found: {}", state_file.string());

        auto state_size = std::filesystem::file_size(state_file);
        std::ifstream state(state_file, std::ifstream::in | std::ifstream::binary);
        if(!state)
            LOG("warning: unable to open state file");
        else
        {
            bool current_error_skip = true;
            bool current_error_c2 = false;
            bool current_success_c2_off = false;
            bool current_success_scsi_off = false;

            const uint32_t state_sector_size = CD_DATA_SIZE / CD_SAMPLE_SIZE;
            char buffer[state_sector_size];
            int32_t lba = cd_detected ? LBA_START : 0;
            while(state.read(buffer, state_sector_size))
            {
                auto bytes_read = state.gcount();
                if(bytes_read == 0)
                    break;
                else if(bytes_read != sizeof(buffer))
                {
                    LOG("warning: unexpected error while reading state file");
                    break;
                }

                uint32_t count_error_skip = std::count(buffer, buffer + state_sector_size, (char)State::ERROR_SKIP);
                if(count_error_skip && !current_error_skip)
                {
                    current_error_skip = true;
                    LOG("LBA {} onwards have samples not read", lba);
                }
                if(!count_error_skip && current_error_skip)
                {
                    current_error_skip = false;
                    LOG("LBA {} onwards no longer have samples not read", lba);
                }

                uint32_t count_error_c2 = std::count(buffer, buffer + state_sector_size, (char)State::ERROR_C2);
                if(count_error_c2 && !current_error_c2)
                {
                    current_error_c2 = true;
                    LOG("LBA {} onwards have C2 errored samples", lba);
                }
                if(!count_error_c2 && current_error_c2)
                {
                    current_error_c2 = false;
                    LOG("LBA {} onwards no longer have C2 errored samples", lba);
                }

                uint32_t count_success_c2_off = std::count(buffer, buffer + state_sector_size, (char)State::SUCCESS_C2_OFF);
                if(count_success_c2_off && !current_success_c2_off)
                {
                    current_success_c2_off = true;
                    LOG("LBA {} onwards have samples read without C2 checking enabled", lba);
                }
                if(!count_success_c2_off && current_success_c2_off)
                {
                    current_success_c2_off = false;
                    LOG("LBA {} onwards no longer have samples read without C2 checking enabled", lba);
                }

                uint32_t count_success_scsi_off = std::count(buffer, buffer + state_sector_size, (char)State::SUCCESS_SCSI_OFF);
                if(count_success_scsi_off && !current_success_scsi_off)
                {
                    current_success_scsi_off = true;
                    LOG("LBA {} onwards have samples read without SCSI read errors enabled", lba);
                }
                if(!count_success_scsi_off && current_success_scsi_off)
                {
                    current_success_scsi_off = false;
                    LOG("LBA {} onwards no longer have samples read without SCSI read errors enabled", lba);
                }

                lba++;
            }
        }
    }
    else
    {
        LOG("");
        LOG("*** warning: state file not found: {}", state_file.string());
    }

    // hash file
    std::vector<std::filesystem::path> hash_files;
    bool hash_found = false;
    hash_files.push_back(file.replace_extension(".hash"));
    for(auto const &t : track_names)
        hash_files.push_back((file.parent_path() / t).replace_extension(".hash"));
    for(auto const &hash_file : hash_files)
    {
        if(!std::filesystem::exists(hash_file))
            continue;
        hash_found = true;
        LOG("");
        LOG("*** found: {}", hash_file.string());
    }
    if(!hash_found)
    {
        LOG("");
        LOG("*** warning: hash file not found: {}", hash_files.front().string());
    }

    // skeleton file
    std::vector<std::filesystem::path> skeleton_files;
    bool skeleton_found = false;
    skeleton_files.push_back(file.replace_extension(".skeleton"));
    for(auto const &t : track_names)
        skeleton_files.push_back((file.parent_path() / t).replace_extension(".skeleton"));
    for(auto const &skeleton_file : skeleton_files)
    {
        if(!std::filesystem::exists(skeleton_file))
            continue;
        skeleton_found = true;

        LOG("");
        LOG("*** found: {}", skeleton_file.string());

        bool iso = false;
        auto dump_size = std::filesystem::file_size(skeleton_file);
        uint32_t sectors_count = 0;
        if(dump_size % 2048 == 0)
        {
            if(dump_size % 2352 == 0)
            {
                std::ifstream file(skeleton_file, std::ifstream::in | std::ifstream::binary);
                char buffer[12];
                if(!file)
                {
                    LOG("error: cannot open skeleton file");
                }
                else
                {
                    file.read(buffer, sizeof(buffer));
                    if(file.gcount() < sizeof(buffer))
                    {
                        LOG("error: failed to read skeleton file");
                    }
                    else if(std::memcmp(buffer, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
                    {
                        iso = true;
                        sectors_count = dump_size / FORM1_DATA_SIZE;
                    }
                    else
                    {
                        iso = false;
                        sectors_count = dump_size / CD_DATA_SIZE;
                    }
                }
            }
            else
            {
                iso = true;
                sectors_count = dump_size / FORM1_DATA_SIZE;
            }
        }
        else if(dump_size % 2352 == 0)
        {
            iso = false;
            sectors_count = dump_size / CD_DATA_SIZE;
        }
        else
        {
            LOG("error: invalid skeleton file size (is it compressed?)");
        }

        if(sectors_count > 0)
        {
            print_skeleton_info(skeleton_file, sectors_count, iso);
            if(verbose)
            {
                LOG("");
                print_iso_fs(skeleton_file, sectors_count, iso);
            }
        }
    }
    if(!skeleton_found)
    {
        LOG("");
        LOG("*** warning: skeleton file not found: {}", skeleton_files.front().string());
    }

    return exit_code;
}

}
