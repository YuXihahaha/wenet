// Copyright 2021 Mobvoi Inc. All Rights Reserved.
// Author: zhendong.peng@mobvoi.com (Zhendong Peng)

#include "decoder/context_graph.h"

#include <utility>

#include "fst/determinize.h"

#include "utils/string.h"

namespace wenet {

ContextGraph::ContextGraph() {}

void ContextGraph::BuildContextGraph(
    const std::vector<std::string>& query_contexts,
    const std::shared_ptr<fst::SymbolTable>& symbol_table) {
  CHECK(symbol_table != nullptr) << "Symbols table should not be nullptr!";
  start_tag_id = symbol_table->AddSymbol("<context>");
  end_tag_id = symbol_table->AddSymbol("</context>");
  symbol_table_ = symbol_table;
  if (query_contexts.empty()) {
    graph_.reset();
    return;
  }

  std::unique_ptr<fst::StdVectorFst> ofst(new fst::StdVectorFst());
  // State 0 is the start state and the final state.
  int start_state = ofst->AddState();
  ofst->SetStart(start_state);
  ofst->SetFinal(start_state, fst::StdArc::Weight::One());

  LOG(INFO) << "Contexts count size: " << query_contexts.size();
  int count = 0;
  for (const auto& context : query_contexts) {
    if (context.size() > config_.max_context_length) {
      LOG(INFO) << "Skip long context: " << context;
      continue;
    }
    if (++count > config_.max_contexts) break;

    std::vector<std::string> words;
    // Split context to words by symbol table, and build the context graph.
    bool no_oov = SplitUTF8StringToWords(Trim(context), symbol_table, &words);
    if (!no_oov) {
      LOG(WARNING) << "Ignore unknown word found during compilation.";
      continue;
    }

    int prev_state = start_state;
    int next_state = start_state;
    for (size_t i = 0; i < words.size(); ++i) {
      int word_id = symbol_table_->Find(words[i]);
      float score = config_.context_score * UTF8StringLength(words[i]);
      next_state = (i < words.size() - 1) ? ofst->AddState() : start_state;
      ofst->AddArc(prev_state,
                   fst::StdArc(word_id, word_id, score, next_state));
      prev_state = next_state;
    }
  }
  std::unique_ptr<fst::StdVectorFst> det_fst(new fst::StdVectorFst());
  fst::Determinize(*ofst, det_fst.get());
  graph_ = std::move(det_fst);
}

ContextState ContextGraph::GetNextState(const ContextState& cur_state,
                                        int word_id) {
  ContextState next_state;
  for (fst::ArcIterator<fst::StdFst> aiter(*graph_, cur_state.sid);
       !aiter.Done(); aiter.Next()) {
    const fst::StdArc& arc = aiter.Value();
    if (arc.ilabel == word_id) {
      next_state.sid = arc.nextstate;
      next_state.score = arc.weight.Value();
      if (cur_state.sid == 0) {
        next_state.is_start_boundary = true;
      }
      if (graph_->Final(arc.nextstate) == fst::StdArc::Weight::One()) {
        next_state.is_end_boundary = true;
      }
      break;
    }
  }
  return next_state;
}

}  // namespace wenet
