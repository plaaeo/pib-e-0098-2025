#include "port/types.hh"
#include "net/types.hh"

namespace net {
constexpr auto TAG = "net";

void CandidateParents::clear()
{
    candidate_parents.clear();
};

void CandidateParents::add_or_update(ParentInfo &&info)
{
    // Buscar potencial pai no vetor `candidate_parents`
    for (size_t i = 0; i < candidate_parents.size(); i++) {
        // Atualizar caso tenhamos encontrado o pai
        if (candidate_parents[i].id == info.id) {
            candidate_parents.assign(info, i);
            return;
        }
    }

    // Tentar inserir no vetor, caso não seja possível, substituir o pior pai
    if (!candidate_parents.push_back(info)) {
        size_t   worstIndex = SIZE_MAX;
        uint32_t worstScore = info.score();

        // Buscar pai com pior pontuação
        for (size_t i = 0; i < candidate_parents.size(); i++) {
            uint32_t score = candidate_parents[i].score();
            if (score < worstScore) {
                worstIndex = i;
                worstScore = score;
            }
        }

        // Descartar pai novo
        if (worstIndex == SIZE_MAX) {
            PORT_LOGI(TAG, "discarding low-quality parent %hhu (new)", info.id);
            return;
        }

        // Descartar pai antigo
        PORT_LOGI(TAG, "discarding low-quality parent %hhu for %hhu",
                  candidate_parents[worstIndex].id, info.id);
        candidate_parents.assign(info, worstIndex);
    }
};

static int compare_parent_info(const void *a, const void *b)
{
    return static_cast<const ParentInfo *>(a)->score() -
           static_cast<const ParentInfo *>(b)->score();
};

port::optional<uint8_t> CandidateParents::sort_by_objective()
{
    if (candidate_parents.size() == 0)
        return port::nullopt;

    // Ordenar usando quicksort
    qsort(candidate_parents.data(), candidate_parents.size(),
          sizeof(ParentInfo), compare_parent_info);

    return port::optional<uint8_t>(candidate_parents[0].id);
};
}  // namespace net