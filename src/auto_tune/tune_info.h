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
#ifndef AUTO_TUNE_TUNE_INFO_H_
#define AUTO_TUNE_TUNE_INFO_H_
#include <tvm/base.h>
#include <tvm/expr.h>
#include <string>

#include "pass/utils.h"
#include "poly/isl.h"
#include "poly/poly_util.h"
#include "codegen/util.h"
#include "common/common_util.h"

namespace akg {
namespace ir {
namespace poly {

constexpr auto MMA_UNIT = 16;
constexpr auto MIN_TILE = 1;
constexpr auto AT_TEMPLATE = "TEMPLATE";
constexpr auto AT_THREAD_CFG = "THREAD_CONFIG";
constexpr auto AT_BLOCK_CFG = "BLOCK_CONFIG";

/*!
 * IslEmitter for GPU
 */
constexpr auto AKG_ALL_REDUCE = "akg_reduce::ALL_REDUCE";
constexpr auto AKG_X_REDUCE = "akg_reduce::REDUCE2D_X";
constexpr auto AKG_Y_REDUCE = "akg_reduce::REDUCE2D_Y";
constexpr auto SCALAR_TENSOR_PREFIX = "acc_";
constexpr auto SCALAR_KHT_PREFIX = "kahan_t";
constexpr auto SCALAR_KHY_PREFIX = "kahan_y";
constexpr auto SCALAR_KHC_PREFIX = "kahan_c";
constexpr auto SHARED_TENSOR_PREFIX = "red_buf";

enum TileLevel { CACHE0 = 0, CACHE1 };

/*
 * This is an light-weight implementation of TileAxis in tvm node way.
 */
class AxisInfoNode : public Node {
  struct Constraint {
    Expr tile_mod_{1};
    Expr tile_min_{1};
    Expr tile_extent_{1};
    std::vector<Expr> cand_factor{};  // list of available factor
  };

 public:
  int index{0};
  std::string dim_axis;
  int64_t range_min;
  Expr range_extent;
  Constraint c1_constraints;
  Constraint c0_constraints;
  bool forbid_iso;
  bool is_inner;
  bool mc_sup;
  std::vector<int64_t> dims;
  std::unordered_map<std::string, std::vector<std::string>> attrs;
  std::unordered_map<std::string, Var> var_names;  // used in inequality solver to record unique var address

  void VisitAttrs(AttrVisitor *v) {
    v->Visit("index", &index);
    v->Visit("dim_axis", &dim_axis);
    v->Visit("range_min", &range_min);
    v->Visit("range_extent", &range_extent);
    v->Visit("c1_constraints.min", &c1_constraints.tile_min_);
    v->Visit("c1_constraints.extent", &c1_constraints.tile_extent_);
    v->Visit("c1_constraints.mod", &c1_constraints.tile_mod_);
    v->Visit("c0_constraints.min", &c0_constraints.tile_min_);
    v->Visit("c0_constraints.extent", &c0_constraints.tile_extent_);
    v->Visit("c0_constraints.mod", &c0_constraints.tile_mod_);
    v->Visit("forbid_iso", &forbid_iso);
    v->Visit("is_inner", &is_inner);
    v->Visit("mc_sup", &mc_sup);
  }

  static constexpr const char *_type_key = "AxisInfoNode";
  TVM_DECLARE_NODE_TYPE_INFO(AxisInfoNode, Node);
};

class AxisInfo : public NodeRef {
 public:
  AxisInfo() {}
  explicit AxisInfo(const ObjectPtr<Object> &n) : NodeRef(n) {}
  ~AxisInfo() {}

  inline AxisInfoNode *operator->() const { return static_cast<AxisInfoNode *>(data_.get()); }
};

TVM_REGISTER_NODE_TYPE(AxisInfoNode);

class TuneInfo {
 public:
  TuneInfo() {}
  ~TuneInfo() {}
  AttrMap analysis;
  std::vector<std::string> axes_names;
  Array<NodeRef> PatternMatch(const std::string &key) {
    Array<NodeRef> res;
    auto RmPrefix = [this](const std::string &key) -> std::string {
      auto sp = akg::common::Split(key, "_");
      if (sp.size() < 2) {
        return key;
      }

      std::string res = "";
      for (size_t i = 1; i < sp.size(); ++i) {
        res += sp[i];
        if (i == sp.size() - 1) {
          break;
        }
        res += "_";
      }
      return res;
    };

    if (analysis.count(RmPrefix(key)) != 0) {
      res.push_back(analysis[RmPrefix(key)]);
    }
    return res;
  }
};

}  // namespace poly
}  // namespace ir
}  // namespace akg

#endif  // AUTO_TUNE_TUNE_INFO_H_
