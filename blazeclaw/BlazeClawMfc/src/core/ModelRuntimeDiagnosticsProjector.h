#pragma once

#include "diagnostics/DiagnosticsSnapshot.h"
#include "OnnxEmbeddingsService.h"
#include "RetrievalMemoryService.h"
#include "runtime/LocalModel/ITextGenerationRuntime.h"

namespace blazeclaw::core {

    class ModelRuntimeDiagnosticsProjector {
    public:
        struct Context {
            const EmbeddingsServiceSnapshot* embeddings = nullptr;
            const localmodel::LocalModelRuntimeSnapshot* localModel = nullptr;
            const RetrievalMemorySnapshot* retrieval = nullptr;
            bool localModelRolloutEligible = false;
            bool localModelActivationEnabled = false;
            std::string localModelActivationReason;
            bool embeddingsConfigFeatureImplemented = false;
        };

        void Apply(
            const Context& context,
            DiagnosticsSnapshot& snapshot) const {
            if (context.embeddings != nullptr) {
                snapshot.embeddingsEnabled = context.embeddings->enabled;
                snapshot.embeddingsReady = context.embeddings->ready;
                snapshot.embeddingsProvider = context.embeddings->provider;
                snapshot.embeddingsStatus = context.embeddings->status;
                snapshot.embeddingsDimension = context.embeddings->dimension;
                snapshot.embeddingsMaxSequenceLength =
                    context.embeddings->maxSequenceLength;
                snapshot.embeddingsModelPathConfigured =
                    !context.embeddings->modelPath.empty();
                snapshot.embeddingsTokenizerPathConfigured =
                    !context.embeddings->tokenizerPath.empty();
                snapshot.embeddingsConfigFeatureImplemented =
                    context.embeddingsConfigFeatureImplemented;
            }

            if (context.localModel != nullptr) {
                snapshot.localModelEnabled = context.localModel->enabled;
                snapshot.localModelReady = context.localModel->ready;
                snapshot.localModelRolloutEligible =
                    context.localModelRolloutEligible;
                snapshot.localModelActivationEnabled =
                    context.localModelActivationEnabled;
                snapshot.localModelActivationReason =
                    context.localModelActivationReason;
                snapshot.localModelProvider = context.localModel->provider;
                snapshot.localModelRolloutStage = context.localModel->rolloutStage;
                snapshot.localModelStorageRoot = context.localModel->storageRoot;
                snapshot.localModelVersion = context.localModel->version;
                snapshot.localModelStatus = context.localModel->status;
                snapshot.localModelVerboseMetrics =
                    context.localModel->verboseMetrics;
                snapshot.localModelRuntimeDllPresent =
                    context.localModel->runtimeDllPresent;
                snapshot.localModelMaxTokens = context.localModel->maxTokens;
                snapshot.localModelTemperature = context.localModel->temperature;
                snapshot.localModelModelLoadAttempts =
                    context.localModel->modelLoadAttempts;
                snapshot.localModelModelLoadFailures =
                    context.localModel->modelLoadFailures;
                snapshot.localModelRequestsStarted =
                    context.localModel->requestsStarted;
                snapshot.localModelRequestsCompleted =
                    context.localModel->requestsCompleted;
                snapshot.localModelRequestsFailed =
                    context.localModel->requestsFailed;
                snapshot.localModelRequestsCancelled =
                    context.localModel->requestsCancelled;
                snapshot.localModelCumulativeTokens =
                    context.localModel->cumulativeTokens;
                snapshot.localModelCumulativeLatencyMs =
                    context.localModel->cumulativeLatencyMs;
                snapshot.localModelLastLatencyMs =
                    context.localModel->lastLatencyMs;
                snapshot.localModelLastGeneratedTokens =
                    context.localModel->lastGeneratedTokens;
                snapshot.localModelLastTokensPerSecond =
                    context.localModel->lastTokensPerSecond;
                snapshot.localModelModelPathConfigured =
                    !context.localModel->modelPath.empty();
                snapshot.localModelModelHashConfigured =
                    !context.localModel->modelExpectedSha256.empty();
                snapshot.localModelModelHashVerified =
                    context.localModel->modelHashVerified;
                snapshot.localModelTokenizerPathConfigured =
                    !context.localModel->tokenizerPath.empty();
                snapshot.localModelTokenizerHashConfigured =
                    !context.localModel->tokenizerExpectedSha256.empty();
                snapshot.localModelTokenizerHashVerified =
                    context.localModel->tokenizerHashVerified;
            }

            if (context.retrieval != nullptr) {
                snapshot.retrievalEnabled = context.retrieval->enabled;
                snapshot.retrievalRecordCount = context.retrieval->recordCount;
                snapshot.retrievalLastQueryCount =
                    context.retrieval->lastQueryCount;
                snapshot.retrievalStatus = context.retrieval->status;
            }
        }
    };

} // namespace blazeclaw::core
