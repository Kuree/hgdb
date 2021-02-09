#include "engine.hh"

#include <regex>

#include "../../src/util.hh"

namespace hgdb::replay {

using reverse_data = hgdb::AVPIProvider::rewind_data;

EmulationEngine::EmulationEngine(ReplayVPIProvider* vcd) : vpi_(vcd) {
    // set callbacks
    vpi_->set_on_cb_added([this](p_cb_data cb_data) { on_cb_added(cb_data); });
    vpi_->set_on_cb_removed([this](const s_cb_data& cb_data) { on_cb_removed(cb_data); });
    vpi_->set_on_reversed([this](reverse_data* reverse_data) { return on_rewound(reverse_data); });

    // set time
    change_time(0);
}

void EmulationEngine::run(bool blocking) {
    auto run_logic = [this]() {
        // we need to pull out the watched value list and see if there is any value change
        vpi_->trigger_cb(cbStartOfSimulation);

        // the loop
        emulation_loop();

        vpi_->trigger_cb(cbEndOfSimulation);
    };

    if (blocking) {
        run_logic();
    } else {
        thread_ = std::thread(run_logic);
    }
}

void EmulationEngine::finish() {
    if (thread_) {
        thread_->join();
    }
}

void EmulationEngine::on_cb_added(p_cb_data cb_data) {
    auto* handle = cb_data->obj;
    if (vpi_->is_valid_handle(handle)) {
        watched_values_.emplace(std::make_pair(handle, std::nullopt));
    }
}

void EmulationEngine::on_cb_removed(const s_cb_data& cb_data) {
    auto* handle = cb_data.obj;
    if (watched_values_.find(handle) != watched_values_.end()) {
        watched_values_.erase(handle);
    }
}

bool EmulationEngine::on_rewound(hgdb::AVPIProvider::rewind_data* rewind_data) {
    uint64_t max_time = rewind_data->time;
    std::vector<uint64_t> times;
    times.reserve(rewind_data->clock_signals.size());
    for (auto* handle : rewind_data->clock_signals) {
        auto signal = vpi_->get_signal_id(handle);
        if (signal) {
            auto time = vpi_->db().get_prev_value_change_time(*signal, max_time, "1");
            if (time) {
                times.emplace_back(*time);
            }
        }
    }
    if (times.empty()) return false;
    std::sort(times.begin(), times.end());
    auto next_time = times.back();
    // move back a little bit so we can evaluate the posedge
    change_time(next_time - 1);
    return true;
}

void EmulationEngine::emulation_loop() {
    while (true) {
        std::vector<uint64_t> times;
        times.reserve(watched_values_.size());
        for (auto const& iter : watched_values_) {
            auto signal_id = vpi_->get_signal_id(iter.first);
            if (signal_id) {
                auto time = vpi_->db().get_next_value_change_time(*signal_id, timestamp_);
                if (time) {
                    times.emplace_back(*time);
                }
            }
        }
        // if there is no time to schedule, we break the loop
        if (times.empty()) break;
        // sort the times in ascending order
        std::sort(times.begin(), times.end());
        // only need the first signal
        auto time = times[0];
        // change time to the place where value changes happens
        change_time(time);
        // retrieve all the watched values and compare their value change
        std::unordered_map<vpiHandle, int64_t> changed_values;
        for (auto const& [handle, pre_value] : watched_values_) {
            s_vpi_value value_p;
            value_p.format = vpiIntVal;
            vpi_->vpi_get_value(handle, &value_p);
            auto current_value = value_p.value.integer;
            if (!pre_value || (pre_value && (*pre_value != current_value))) {
                // this is a value change
                changed_values.emplace(handle, current_value);
            }
        }
        // notice that since we are doing emulation, at the callback only that particular
        // value is changed properly, the rest is still "lagging" behind, the we actually
        // need to fetch the time at (time - 1)
        change_time(time - 1);
        // need to file the callbacks
        // notice that we need to be very careful about the sequence of firing callback
        // in case that during the firing, client has request to reverse timestamp
        vpi_->clear_overridden_values();
        for (auto const& [handle, new_value] : changed_values) {
            vpi_->set_is_callback_eval(true);
            // set the overridden value
            vpi_->add_overridden_value(handle, new_value);
            vpi_->trigger_cb(cbValueChange, handle, new_value);
            vpi_->set_is_callback_eval(false);
            // notice that we need to do a check on whether time has changed
            if (timestamp_.load() != (time - 1)) {
                // time has changed, abort
                break;
            }
        }
        // clear out overridden values
        vpi_->clear_overridden_values();

        // advance the timestamp
        // depends on the timestamp value, we decides differently
        // there might be a race condition here
        // i.e. when we are doing comparison and user send a time change request
        // however this is an undefined behavior since we don't expect user to send
        // requests when the simulator is running. only when the simulator is paused is
        // the request well-defined. in this case, inside the trigger_cb call
        if (timestamp_.load() == (time - 1)) {
            change_time(time);
        }
    }
}

void EmulationEngine::change_time(uint64_t time) {
    timestamp_ = time;
    vpi_->set_timestamp(time);
}

void EmulationEngine::build_array_table(const std::vector<std::string>& rtl_names) {
    // need to filter out the signal of interests
    std::set<std::string> array_signals;
    auto re = std::regex(R"([\.\[]\d+)");
    for (auto const& name : rtl_names) {
        if (std::regex_match(name, re)) {
            array_signals.emplace(name);
        }
    }
    for (auto const& name : array_signals) {
        auto tokens = util::get_tokens(name, ".[]");
        // search backward to find the root signal
        // notice that we don't support generate construct since it's too much work to deal with
        std::optional<uint64_t> start_index;
        for (uint64_t index = tokens.size(); index != 0; index--) {
            auto i = index - 1;
            auto n = tokens[i];
            if (std::all_of(n.begin(), n.end(), isdigit)) {
                continue;
            }
            start_index = i;
            break;
        }
        if (!start_index) {
            // something went wrong?
            continue;
        }
        // found a non-digit root
        auto handle_name = util::join(tokens.begin(), tokens.end() + *start_index, ".");
        auto array_index = std::stoul(tokens[*start_index]);
        std::vector<uint64_t> slices = {array_index};

        // see if we can find the handle name or not
        auto* handle = vpi_->vpi_handle_by_name(const_cast<char*>(handle_name.c_str()), nullptr);
        if (!handle) {
            // could be unpacked array
            continue;
        }
        // fill out the array able
        auto& array = vpi_->array_map_[handle];
        if (array.size() < (array_index + 1)) {
            array.resize(array_index + 1, nullptr);
        }
        auto* array_handle = vpi_->get_new_handle();
        array[array_index] = array_handle;
        for (uint64_t idx = array_index + 1; idx < tokens.size(); idx++) {
            auto index = std::stoul(tokens[idx]);
            auto array_ = vpi_->array_map_[array_handle];
            if (array_.size() < (index + 1)) {
                array_.resize(index + 1, nullptr);
            }
            array_handle = vpi_->get_new_handle();
            array_[index] = array_handle;
            slices.emplace_back(index);
        }

        // put it into the array info
        vpi_->array_info_.emplace(array_handle, std::make_pair(handle, slices));
    }
}

}  // namespace hgdb::replay
