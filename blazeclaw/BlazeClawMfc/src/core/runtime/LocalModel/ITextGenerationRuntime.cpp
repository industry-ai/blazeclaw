#include "pch.h"
#include "ITextGenerationRuntime.h"

namespace blazeclaw::core::localmodel {

std::string TextGenerationErrorCodeToString(
    const TextGenerationErrorCode code) {
  switch (code) {
    case TextGenerationErrorCode::None:
      return "none";
    case TextGenerationErrorCode::LocalModelDisabled:
      return "local_model_disabled";
    case TextGenerationErrorCode::ProviderNotSupported:
      return "provider_not_supported";
    case TextGenerationErrorCode::ModelNotFound:
      return "model_not_found";
    case TextGenerationErrorCode::ModelLoadFailed:
      return "model_load_failed";
    case TextGenerationErrorCode::TokenizerNotFound:
      return "tokenizer_not_found";
    case TextGenerationErrorCode::TokenizationFailed:
      return "tokenization_failed";
    case TextGenerationErrorCode::InvalidInput:
      return "invalid_input";
    case TextGenerationErrorCode::InputTooLarge:
      return "input_too_large";
    case TextGenerationErrorCode::EmptyOutput:
      return "local_model_empty_output";
    case TextGenerationErrorCode::InferenceFailed:
      return "inference_failed";
    case TextGenerationErrorCode::RuntimeUnavailable:
      return "runtime_unavailable";
    case TextGenerationErrorCode::Cancelled:
      return "cancelled";
    default:
      return "unknown";
  }
}

} // namespace blazeclaw::core::localmodel
