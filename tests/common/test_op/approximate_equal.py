# Copyright 2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""operator dsl function: approximate_equal"""
import akg.tvm
from akg.utils import kernel_exec as utils
from akg.utils import validation_check as vc_util
from akg.utils.format_transform import get_shape
from akg.ops.math.sub import sub
from akg.ops.math.abs import abs_value
from akg.ops.math.cast import cast


@vc_util.check_input_type(akg.tvm.tensor.Tensor, akg.tvm.tensor.Tensor, (float, type(None)))
def approximate_equal(x, y, tolerance=1e-5):
    """
    abs(x-y) less than or equal to the tolerance

    Args:
        x (tvm.tensor.Tensor): Tensor of type float16, float32.
        y (tvm.tensor.Tensor): Tensor of type float16, float32.
        tolerance (float): default is 1e-5

    Returns:
        tvm.tensor.Tensor. If abs(x-y) less than or equal to the tolerance return True,
        else return False.
    """

    if tolerance < 0:
        raise RuntimeError("tolerance should >= 0")

    # check shape
    vc_util.check_shape(x)
    vc_util.check_shape(y)
    shape = get_shape(x)
    if shape != get_shape(y):
        raise RuntimeError("input shape must be same, but got %s vs %s",
                           shape, get_shape(y))

    # check input tensor data_type
    vc_util.ops_dtype_check(x.dtype, vc_util.DtypeForDavinci.ALL_FLOAT)
    vc_util.ops_dtype_check(y.dtype, vc_util.DtypeForDavinci.ALL_FLOAT)
    dtype = x.dtype
    if dtype != y.dtype:
        raise RuntimeError("input type must be same, but got %s  vs %s",
                           dtype, y.dtype)

    res_vsub = sub(x, y)
    res_vabs = abs_value(res_vsub)

    # As vcmp_lt and vsel instruction don't support fp32 on mini
    # It can be simplified by some methods, such as , "auto cast"
    if utils.product_is_mini():
        dtype = "float16"
        res_vabs = cast(res_vabs, dtype)

    t = akg.tvm.compute(shape, lambda *indice: akg.tvm.const(1, dtype), "t")
    f = akg.tvm.compute(shape, lambda *indice: akg.tvm.const(0, dtype), "f")
    res = akg.tvm.compute(shape, lambda *indice: akg.tvm.expr.Select(
        res_vabs[indice] <= akg.tvm.const(tolerance, dtype),
        t[indice], f[indice]))

    #  It can be be simplified that let cast op support fp16 and fp32 to bool type
    res_fp16 = cast(res, "float16")
    res_bool = akg.tvm.compute(shape, lambda *indice: res_fp16(*indice).astype("bool"))
    return res_bool
