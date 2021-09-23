#include "fsdb.hh"

#include <regex>
#include <stack>
#include <vector>

#include "ffrAPI.h"

// based on the fsdb2vcd_fast
// converted for modern C++
// https://github.com/gtkwave/gtkwave/blob/master/gtkwave3-gtk3/contrib/fsdb2vcd/fsdb2vcd_fast.cc

// notice that I tried my best to avoid memory leaks but there are several leaks reported by
// valgrind.

struct parser_info {
public:
    std::vector<std::string> scopes;
    std::unordered_map<uint64_t, WaveformInstance> instance_map;
    // for fast look up
    std::unordered_map<std::string, uint64_t> instance_name_map;
    std::unordered_map<uint64_t, WaveformSignal> variable_map;
    std::unordered_map<std::string, uint64_t> variable_id_map;

    std::unordered_map<uint64_t, std::vector<uint64_t>> instance_vars;
    std::unordered_map<uint64_t, std::vector<uint64_t>> instance_hierarchy;

    // used to map the instance names
    // key is the module name and values are instance ids
    // this is designed to reduce memory overhead in case of lots of instances
    std::unordered_map<std::string, std::unordered_set<uint64_t>> instance_def_map;

    std::optional<uint64_t> top_instance_id;

    // tracking information
    std::stack<uint64_t> current_instance_ids;

    // this full name includes a '.' at the very end
    [[nodiscard]] std::string full_name() const {
        std::string result;
        for (auto const &scope : scopes) {
            result.append(scope);
            result.append(".");
        }
        return result;
    }
};

static bool_T null_trace_cb(fsdbTreeCBType, void *, void *) { return true; }

// NOLINTNEXTLINE
static bool_T parse_var_def(fsdbTreeCBType cb_type, void *client_data, void *tree_cb_data) {
    auto *info = reinterpret_cast<parser_info *>(client_data);
    auto &scopes = info->scopes;

    switch (cb_type) {
        case FSDB_TREE_CBT_BEGIN_TREE: {
            break;
        }

        case FSDB_TREE_CBT_SCOPE: {
            auto *scope = reinterpret_cast<fsdbTreeCBDataScope *>(tree_cb_data);
            // get id setting
            auto full_name = info->full_name();
            full_name.append(scope->name);
            auto id = info->instance_map.size();
            info->instance_map.emplace(id, WaveformInstance{id, full_name});
            info->instance_name_map.emplace(full_name, id);
            scopes.emplace_back(scope->name);
            auto scope_type = static_cast<int>(scope->type);
            // we are not interested in the hidden scope
            if ((scope_type == FSDB_ST_SV_INTERFACE || scope_type == FSDB_ST_VCD_MODULE) &&
                !scope->is_hidden_scope) {
                // parent info
                if (!info->current_instance_ids.empty()) {
                    auto parent_id = info->current_instance_ids.top();
                    info->instance_hierarchy[parent_id].emplace_back(id);
                }
                info->current_instance_ids.emplace(id);
                if (scope_type == FSDB_ST_VCD_MODULE && scope->module) {
                    info->instance_def_map[scope->module].emplace(id);
                    if (!info->top_instance_id) {
                        // the first one is always the TOP
                        info->top_instance_id = id;
                    }
                }
            }
            break;
        }

        case FSDB_TREE_CBT_STRUCT_BEGIN: {
            auto *s = reinterpret_cast<fsdbTreeCBDataStructBegin *>(tree_cb_data);
            // treat struct as a scope
            auto full_name = info->full_name();
            full_name.append(s->name);
            auto id = info->instance_map.size();
            info->instance_map.emplace(id, WaveformInstance{id, full_name});
            info->instance_name_map.emplace(full_name, id);
            scopes.emplace_back(s->name);
            break;
        }

        case FSDB_TREE_CBT_VAR: {
            auto *var = reinterpret_cast<fsdbTreeCBDataVar *>(tree_cb_data);
            std::string name = var->name;
            auto full_name = info->full_name();
            full_name.append(name);
            uint32_t width;
            if (var->type == FSDB_VT_VCD_REAL) {
                width = 64;
            } else {
                if (var->lbitnum >= var->rbitnum) {
                    width = var->lbitnum - var->rbitnum + 1;
                } else {
                    width = var->rbitnum - var->lbitnum + 1;
                }
            }
            // one thing to notice is that FSDB adds width to the end if it's greater than 1
            if (width > 1) {
                auto pos = full_name.find_last_of('[');
                full_name = full_name.substr(0, pos);
            }
            // we use the ID inside FSDB to avoid extra layer of translation
            auto id = static_cast<uint64_t>(var->u.idcode);
            info->variable_map.emplace(id, WaveformSignal{id, full_name, width});
            info->variable_id_map.emplace(full_name, id);
            // map parent
            if (info->current_instance_ids.empty()) {
                throw std::runtime_error("Unable to find parent scope");
            } else {
                auto inst_id = info->current_instance_ids.top();
                info->instance_vars[inst_id].emplace_back(id);
            }
            break;
        }

        case FSDB_TREE_CBT_STRUCT_END: {
            scopes.pop_back();
            break;
        }
        case FSDB_TREE_CBT_UPSCOPE: {
            auto *scope = reinterpret_cast<fsdbTreeCBDataScope *>(tree_cb_data);
            auto scope_type = static_cast<int>(scope->type);
            scopes.pop_back();
            // notice that we need to be symmetric
            if (scope_type == FSDB_ST_SV_INTERFACE || scope_type == FSDB_ST_VCD_MODULE) {
                info->current_instance_ids.pop();
            }
            break;
        }

        case FSDB_TREE_CBT_FILE_TYPE:
        case FSDB_TREE_CBT_SIMULATOR_VERSION:
        case FSDB_TREE_CBT_SIMULATION_DATE:
        case FSDB_TREE_CBT_X_AXIS_SCALE:
        case FSDB_TREE_CBT_END_ALL_TREE:
        case FSDB_TREE_CBT_RECORD_BEGIN:
        case FSDB_TREE_CBT_RECORD_END:
        case FSDB_TREE_CBT_END_TREE:
        case FSDB_TREE_CBT_ARRAY_BEGIN:
        case FSDB_TREE_CBT_ARRAY_END:
            break;

        default:
            return false;
    }

    return true;
}

namespace hgdb::fsdb {

FSDBProvider::FSDBProvider(const std::string &filename) {
    auto *filename_ptr = const_cast<char *>(filename.c_str());
    if (!ffrObject::ffrIsFSDB(filename_ptr)) {
        throw std::runtime_error("Invalid filename " + filename);
    }

    ffrFSDBInfo fsdb_info;
    ffrObject::ffrGetFSDBInfo(filename_ptr, fsdb_info);
    if ((fsdb_info.file_type != FSDB_FT_VERILOG) && (fsdb_info.file_type != FSDB_FT_VERILOG_VHDL) &&
        (fsdb_info.file_type != FSDB_FT_VHDL)) {
        throw std::runtime_error("Invalid FSDB " + filename);
    }

    fsdb_ = ffrObject::ffrOpen3(filename_ptr);
    if (!fsdb_) {
        throw std::runtime_error("Invalid FSDB " + filename);
    }

    fsdb_->ffrSetTreeCBFunc(null_trace_cb, nullptr);

    auto ft = fsdb_->ffrGetFileType();
    if ((ft != FSDB_FT_VERILOG) && (ft != FSDB_FT_VERILOG_VHDL) && (ft != FSDB_FT_VHDL)) {
        fsdb_->ffrClose();
        throw std::runtime_error("Invalid FSDB " + filename);
    }

    uint_T blk_idx = 0;
    /* necessary if FSDB file has transaction data ... we don't process this but it prevents
     * possible crashes */
    fsdb_->ffrReadDataTypeDefByBlkIdx(blk_idx);

    // start the parsing process
    parser_info info;

    fsdb_->ffrSetTreeCBFunc(parse_var_def, &info);
    fsdb_->ffrReadScopeVarTree();

    if (!info.scopes.empty()) {
        throw std::runtime_error("Incomplete scope " + info.full_name());
    }

    instance_map_ = std::move(info.instance_map);
    instance_name_map_ = std::move(info.instance_name_map);
    variable_map_ = std::move(info.variable_map);
    variable_id_map_ = std::move(info.variable_id_map);
    instance_vars_ = std::move(info.instance_vars);
    instance_hierarchy_ = std::move(info.instance_hierarchy);
    instance_def_map_ = std::move(info.instance_def_map);

    if (info.top_instance_id) top_instance_ = *info.top_instance_id;
}

std::optional<uint64_t> FSDBProvider::get_instance_id(const std::string &full_name) {
    if (instance_name_map_.find(full_name) != instance_name_map_.end()) {
        return instance_name_map_.at(full_name);
    } else {
        return std::nullopt;
    }
}

std::optional<uint64_t> FSDBProvider::get_signal_id(const std::string &full_name) {
    if (variable_id_map_.find(full_name) != variable_id_map_.end()) {
        return variable_id_map_.at(full_name);
    } else {
        // trying array dot conversion
        const static std::regex pattern(R"(\.(\d+))");
        auto new_name = std::regex_replace(full_name, pattern, R"([$1])");
        if (variable_id_map_.find(new_name) != variable_id_map_.end()) {
            return variable_id_map_.at(new_name);
        }
        return std::nullopt;
    }
}

std::vector<WaveformSignal> FSDBProvider::get_instance_signals(uint64_t instance_id) {
    if (instance_vars_.find(instance_id) != instance_vars_.end()) {
        auto const &ids = instance_vars_.at(instance_id);
        std::vector<WaveformSignal> result;
        result.reserve(ids.size());
        for (auto id : ids) {
            result.emplace_back(variable_map_.at(id));
        }
        return result;
    } else {
        return {};
    }
}

std::vector<WaveformInstance> FSDBProvider::get_child_instances(uint64_t instance_id) {
    if (instance_hierarchy_.find(instance_id) != instance_hierarchy_.end()) {
        auto const &ids = instance_hierarchy_.at(instance_id);
        std::vector<WaveformInstance> result;
        result.reserve(ids.size());
        for (auto id : ids) {
            result.emplace_back(instance_map_.at(id));
        }
        return result;
    } else {
        return {};
    }
}

std::optional<WaveformSignal> FSDBProvider::get_signal(uint64_t signal_id) {
    if (variable_map_.find(signal_id) != variable_map_.end()) {
        return variable_map_.at(signal_id);
    } else {
        return std::nullopt;
    }
}

std::optional<std::string> FSDBProvider::get_instance(uint64_t instance_id) {
    if (instance_map_.find(instance_id) != instance_map_.end()) {
        return instance_map_.at(instance_id).name;
    } else {
        return std::nullopt;
    }
}

// helper function to convert FSDB value to vcd string
std::optional<std::string> to_vcd_value(ushort_T bit_size, fsdbBytesPerBit bytes_per_bit,
                                        byte_T *vc_ptr) {
    uint32_t byte_count;
    switch (bytes_per_bit) {
        case FSDB_BYTES_PER_BIT_1B:
            byte_count = 1 * bit_size;
            break;
        case FSDB_BYTES_PER_BIT_2B:
            byte_count = 2 * bit_size;
            break;
        case FSDB_BYTES_PER_BIT_4B:
            byte_count = 4 * bit_size;
            break;
        case FSDB_BYTES_PER_BIT_8B:
            byte_count = 8 * bit_size;
            break;
        default:
            return std::nullopt;
    }
    std::string result;
    for (auto i = 0u; i < byte_count; i++, vc_ptr++) {
        auto vc_type = *vc_ptr;
        switch (vc_type) {
            case FSDB_BT_VCD_0:
                result.append("0");
                break;
            case FSDB_BT_VCD_1:
                result.append("1");
                break;
            case FSDB_BT_VCD_X:
                result.append("x");
                break;
            case FSDB_BT_VCD_Z:
                result.append("z");
                break;
            default:
                return std::nullopt;
        }
    }

    return result;
}

std::optional<std::string> FSDBProvider::get_signal_value(uint64_t id, uint64_t timestamp) {
    auto signal = get_signal(id);
    if (!signal) return std::nullopt;
    // now need to construct handler and query the value change
    auto *hdl = fsdb_->ffrCreateVCTrvsHdl(static_cast<int64_t>(id));
    if (!hdl) {
        return std::nullopt;
    }

    // we will try to jump to that particular time
    fsdbTag64 time;
    time.H = static_cast<uint32_t>(timestamp >> 32);
    time.L = static_cast<uint32_t>(timestamp & 0xFFFFFFFF);

    auto res = hdl->ffrGotoXTag(&time);
    if (res != FSDB_RC_SUCCESS) {
        // failure to jump time
        hdl->ffrFree();
        return std::nullopt;
    }
    // get raw data
    byte_T *vc_ptr;
    hdl->ffrGetVC(&vc_ptr);
    // for consistency reason we convert to VCD-style string
    auto r = to_vcd_value(hdl->ffrGetBitSize(), hdl->ffrGetBytesPerBit(), vc_ptr);
    hdl->ffrFree();
    return r;
}

std::string FSDBProvider::get_full_signal_name(uint64_t signal_id) {
    auto sig = get_signal(signal_id);
    if (sig) {
        return sig->name;
    } else {
        return {};
    }
}

std::string FSDBProvider::get_full_instance_name(uint64_t instance_id) {
    auto inst = get_instance(instance_id);
    if (inst) {
        return *inst;
    } else {
        return {};
    }
}

std::optional<uint64_t> FSDBProvider::get_next_value_change_time(uint64_t signal_id,
                                                                 uint64_t base_time) {
    auto signal = get_signal(signal_id);
    if (!signal) return std::nullopt;
    // now need to construct handler and query the value change
    auto *hdl = fsdb_->ffrCreateVCTrvsHdl(static_cast<int64_t>(signal_id));
    if (!hdl) {
        return std::nullopt;
    }

    // we will try to jump to that particular time
    fsdbTag64 time;
    time.H = static_cast<uint32_t>(base_time >> 32);
    time.L = static_cast<uint32_t>(base_time & 0xFFFFFFFF);

    auto res = hdl->ffrGotoXTag(&time);
    if (res != FSDB_RC_SUCCESS) {
        // failure to jump time
        hdl->ffrFree();
        return std::nullopt;
    }

    hdl->ffrGotoNextVC();
    hdl->ffrGetXTag(&time);
    uint64_t r = time.L;
    r |= static_cast<uint64_t>(time.H) << 32;
    hdl->ffrFree();
    if (r == base_time)
        return std::nullopt;
    else
        return r;
}

std::optional<uint64_t> FSDBProvider::get_prev_value_change_time(uint64_t signal_id,
                                                                 uint64_t base_time,
                                                                 const std::string &target_value) {
    auto signal = get_signal(signal_id);
    if (!signal) return std::nullopt;
    // now need to construct handler and query the value change
    auto *hdl = fsdb_->ffrCreateVCTrvsHdl(static_cast<int64_t>(signal_id));
    if (!hdl) {
        return std::nullopt;
    }

    // we will try to jump to that particular time
    fsdbTag64 time;
    time.H = static_cast<uint32_t>(base_time >> 32);
    time.L = static_cast<uint32_t>(base_time & 0xFFFFFFFF);

    auto res = hdl->ffrGotoXTag(&time);
    if (res != FSDB_RC_SUCCESS) {
        // failure to jump time
        hdl->ffrFree();
        return std::nullopt;
    }
    while (true) {
        hdl->ffrGotoPrevVC();
        byte_T *vc_ptr;
        hdl->ffrGetVC(&vc_ptr);
        auto str = to_vcd_value(hdl->ffrGetBitSize(), hdl->ffrGetBytesPerBit(), vc_ptr);
        if (str == target_value) {
            hdl->ffrGetXTag(&time);
            uint64_t r = time.L;
            r |= static_cast<uint64_t>(time.H) << 32;
            hdl->ffrFree();
            return r;
        }
    }
}

std::optional<std::string> FSDBProvider::get_instance_definition(uint64_t instance_id) const {
    if (instance_map_.find(instance_id) != instance_map_.end()) {
        // need to find a match
        for (auto const &[def, ids] : instance_def_map_) {
            if (ids.find(instance_id) != ids.end()) {
                return def;
            }
        }
    }
    return std::nullopt;
}

FSDBProvider::~FSDBProvider() {
    if (fsdb_) {
        fsdb_->ffrFreeNavDB();
        fsdb_->ffrClose();
    }
}

}  // namespace hgdb::fsdb