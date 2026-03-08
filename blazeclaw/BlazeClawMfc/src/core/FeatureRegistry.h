#pragma once

#include "../pch.h"

namespace blazeclaw::core {

enum class FeatureState {
  Planned,
  InProgress,
  Implemented,
};

struct FeatureEntry {
  std::wstring name;
  FeatureState state = FeatureState::Planned;
};

class FeatureRegistry {
public:
  FeatureRegistry();

  [[nodiscard]] const std::vector<FeatureEntry>& Features() const noexcept;
  [[nodiscard]] bool IsImplemented(const std::wstring& name) const;

private:
  std::vector<FeatureEntry> m_features;
};

} // namespace blazeclaw::core
