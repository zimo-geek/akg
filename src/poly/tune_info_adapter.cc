/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "tune_info_adapter.h"

namespace akg {
namespace ir {
namespace poly {

Var GetAxisDescId(TileAxis *a) {
  std::string var_name = std::to_string(a->index) + "_";
  var_name += a->axis_type_.empty() ? std::to_string(a->dim_axis) : a->axis_type_;
  return Var(var_name);
}

TuneAxisInfo AxisInfoAdapter(TileAxis *a, TileSizes dims) {
  auto axis_info = make_node<TuneAxisInfoNode>();
  axis_info->index = a->index;
  if (a->axis_type_.empty()) {
    axis_info->dim_axis = std::to_string(a->dim_axis);
  } else {
    axis_info->dim_axis = a->axis_type_;
  }
  axis_info->range_min = a->range_min;
  axis_info->range_extent = a->range_extent;
  axis_info->c1_constraints.tile_mod_ = a->c1_constraints.tile_mod_;
  axis_info->c1_constraints.tile_min_ = a->c1_constraints.tile_min_;
  axis_info->c1_constraints.tile_extent_ = a->c1_constraints.tile_extent_;
  axis_info->c0_constraints.tile_mod_ = a->c0_constraints.tile_mod_;
  axis_info->c0_constraints.tile_min_ = a->c0_constraints.tile_min_;
  axis_info->c0_constraints.tile_extent_ = a->c0_constraints.tile_extent_;
  axis_info->forbid_iso = a->forbid_iso;
  axis_info->is_inner = a->is_inner;
  axis_info->mc_sup = a->mc_sup;

  for (auto d : dims) {
    if (axis_info->index == d.index && axis_info->dim_axis == d.axis) {
      axis_info->dims.push_back(d.c1_tiling_size);
      axis_info->dims.push_back(d.c0_tiling_size);
      break;
    }
  }
  if (axis_info->dims.empty()) {
    axis_info->dims.push_back(MIN_TILE);
  }
  for (const auto attr : a->attrs) {
    if (axis_info->attrs.find(attr.attr_key) != axis_info->attrs.end()) {
      axis_info->attrs[attr.attr_key].push_back(attr.attr_value);
    } else {
      axis_info->attrs[attr.attr_key] = {attr.attr_value};
    }
  }
  axis_info->var_names = a->var_names;
  return TuneAxisInfo(axis_info);
}

void ScopInfoAdapter(TuneInfo *tune_info, ScopInfo *scop_info) {
  std::string platform = "cpu";
  if (scop_info->user_config_.GetTarget() == TARGET_CCE) {
    platform = "npu";
  } else if (scop_info->user_config_.GetTarget() == TARGET_CUDA) {
    platform = "gpu";
  }
  tune_info->analysis.Set("platform", StringImm::make(platform));
  if (platform == "npu") {
    tune_info->analysis.Set("max_core_num", make_const(Int(64), GetCoreNumConf()));
  }
  std::string op_template = tune_info->analysis.GetStr("op_template", "");
  if (op_template.empty()) {
    if (scop_info->mmu_info_.HasCube()) {
      op_template = "CUBE";
      tune_info->analysis.Set("mma_m", make_const(Int(64), MMA_UNIT));
    } else {
      op_template = "DEFAULT";
    }
    tune_info->analysis.Set("op_template", StringImm::make(op_template));
  } else if (op_template == "CONV" || op_template == "MATMUL") {
    auto mma = scop_info->analysis_result_.GetMmaMode();
    tune_info->analysis.Set("mma_m", make_const(Int(64), mma.m));
    tune_info->analysis.Set("mma_n", make_const(Int(64), mma.n));
    tune_info->analysis.Set("mma_k", make_const(Int(64), mma.k));
  }

  tune_info->analysis.Set("tensor_of_tensor", make_const(Int(32), scop_info->analysis_result_.GetTensorOfTensor()));
  tune_info->analysis.Set("enable_reduce_lib", make_const(Int(32), scop_info->user_config_.GetEnableAkgReduceLib()));
  tune_info->analysis.Set("enable_atomic_add", make_const(Int(32), scop_info->user_config_.GetEnableAtomicAdd()));
  for (int i = 0; i < scop_info->analysis_result_.GetOuterBandNumber(); ++i) {
    auto reduce_direction = scop_info->analysis_result_.GetOuterBandNode(i)->reduce_direction;
    tune_info->analysis.Set("reduce_direction_" + std::to_string(i),
                            StringImm::make(scop_info->analysis_result_.ShowReduceDirection(reduce_direction)));
  }
}

std::unique_ptr<TuneInfo> AdaptTuneInfo(const TilingAnalyzer &analyzer, ScopInfo *scop_info,
                                        Array<Expr> memory_constraints, TileSizes dims) {
  TuneInfo *tune_info = new TuneInfo();
  tune_info->analysis.Set("memory_constraints", memory_constraints);

  auto CollectTileAxis = [&tune_info, &analyzer, &scop_info, &dims](TileAxis *a) {
    if (a == analyzer.RootAxis()) {
      if (scop_info->user_config_.GetTarget() == TARGET_CPU) {
        tune_info->analysis.Set("op_template", StringImm::make("CPU"));
      } else {
        auto t = scop_info->analysis_result_.ShowOpTemplate();
        tune_info->analysis.Set("op_template", StringImm::make(t));
      }
      auto axis_info = make_node<TuneAxisInfoNode>();
      for (const auto attr : a->attrs) {
        if (axis_info->attrs.find(attr.attr_key) != axis_info->attrs.end()) {
          axis_info->attrs[attr.attr_key].push_back(attr.attr_value);
        } else {
          axis_info->attrs[attr.attr_key] = {attr.attr_value};
        }
      }
      tune_info->analysis.Set("root", TuneAxisInfo(axis_info));
      return;
    }

    std::string name = GetAxisDescId(a)->name_hint;
    auto axis_info = AxisInfoAdapter(a, dims);
    tune_info->analysis.Set(name, TuneAxisInfo(axis_info));
    tune_info->axes_names.push_back(name);
  };

  analyzer.ForEachAxisTopDown(CollectTileAxis);
  ScopInfoAdapter(tune_info, scop_info);
  return std::unique_ptr<TuneInfo>(tune_info);
}

std::unique_ptr<TuneInfo> GenerateTuningInfo(const isl::schedule &sch, ScopInfo *scop_info, Stmt body) {
  // 1. disable tuning to get auto-tiling analysis results
  scop_info->user_config_.SetIsTuning(false);
  TilingAnalyzer analyzer(sch, *scop_info, body);
  bool need_tiling = analyzer.Prepare();
  std::stringstream ss;
  ss << body;
  LOG(INFO) << "Create space for " << body;
  analyzer.GetTileLogger().AppendLog(DO_TUNING, ss);

  // 2. enable tuning to solve and get auto-tiling configs
  scop_info->user_config_.SetIsTuning(true);
  analyzer.scop_info_.user_config_.SetIsTuning(true);
  TileSizes dims = NullTiling();
  Array<Expr> memory_constraints;
  if (!need_tiling) {
    LOG(INFO) << "No space for tuning, exit.";
    if (!analyzer.GetTileLogger().DumpLogFile()) LOG(WARNING) << "Write tiling log fail.";
  } else {
    TilingGenerator generator(analyzer);
    dims = generator.GenerateQuickly();
    for (auto d : dims) {
      LOG(INFO) << d.index << "_" << d.axis << " l1 = " << d.c1_tiling_size << ", l0 = " << d.c0_tiling_size;
    }
    memory_constraints = std::move(generator.memory_constraints_);
  }

  if (!analyzer.GetTileLogger().DumpLogFile()) LOG(WARNING) << "Write tiling log fail.";

  // 3. get tune info
  auto tune_info = AdaptTuneInfo(analyzer, scop_info, memory_constraints, dims);
  tune_info->analysis.Set("need_tiling", make_const(Int(32), need_tiling));
  return tune_info;
}

}  // namespace poly
}  // namespace ir
}  // namespace akg
