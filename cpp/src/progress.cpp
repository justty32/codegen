#include "progress.hpp"
#include <iostream>

void print_plan(const std::vector<fs::path>& targets) {
    int n = (int)targets.size();
    std::cout << "codegen: 將依序處理 " << n << " 個目標：\n";
    for (int i = 0; i < n; i++)
        std::cout << "  [" << (i + 1) << "/" << n << "] "
                  << targets[i].string() << "\n";
}

void report_interrupt(const RunState& state, const std::string& kind) {
    int n = (int)state.targets.size();
    if (kind == "sigint") {
        std::cerr << "\n^C\n";
        std::cerr << "codegen: 已中止。當前處理至：\n";
    } else {
        std::cerr << "\ncodegen: 已中止（abort_all 觸發）。當前處理至：\n";
    }
    if (state.target_idx > 0 && state.target_idx <= n) {
        std::cerr << "  目標：[" << state.target_idx << "/" << n << "] "
                  << state.targets[state.target_idx - 1].string() << "\n";
    }
    if (state.current_file)
        std::cerr << "  檔案：" << state.current_file->string() << "\n";
    if (state.block_ordinal)
        std::cerr << "  Block：第 " << *state.block_ordinal
                  << " 個（行 " << state.block_start_line.value_or(0) << "）\n";
    if (kind == "abort_all" && state.last_failure_reason)
        std::cerr << "  失敗原因：" << *state.last_failure_reason << "\n";
}
