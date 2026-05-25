#pragma once
#include "config.hpp"
#include "env.hpp"
#include "parser.hpp"
#include "scope.hpp"
#include <string>
#include <utility>

// Expand a single top-level block through up to max_passes rounds (§6.2).
// Raises BlockFailure on error.
std::string expand_block(
    const Block& block,
    const Config& parent_cfg,
    ScopeStore& scope,
    const RunContext& ctx);

// Process all top-level blocks in content.
// Returns (updated_text, had_failure).
std::pair<std::string, bool> process_content(
    const std::string& content,
    const Config& cfg,
    ScopeStore& scope,
    const RunContext& ctx);

// Emit block failure diagnostic to stderr (§10.4).
void emit_failure(const BlockFailure& exc);
