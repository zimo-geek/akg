# Copyright 2020-2021 Huawei Technologies Co., Ltd
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
# limitations under the License
import numpy as np
from akg.utils import kernel_exec as utils
from akg.utils.result_analysis import gpu_profiling
from akg.utils.format_transform import to_tvm_nd_array
from akg.ops.math_gpu.reduce_or import reduce_or

def gen_data(in_shape, in_dtype, axis, keepdims):
    support_list = {"bool":np.bool}
    data = np.random.randint(0, 2, (in_shape)).astype(
        support_list[in_dtype])
    expect = np.any(data, axis=axis, keepdims=keepdims)
    if axis == None and keepdims == False:
        expect = np.broadcast_to(expect, (1,))
    output = np.full(expect.shape, 0, in_dtype)
    return data, output, expect


def test_ms_reduce_or(in_shape, in_dtype, axis=None, keepdims=False, poly_sch=False):
    if poly_sch:
        mod = utils.op_build_test(reduce_or, (in_shape, ), (in_dtype, ), op_attrs=[
                             axis, keepdims], kernel_name="reduce_or", attrs={"target": "cuda", 
                             "enable_akg_reduce_lib": True, "enable_atomic_add": False})

    data, output, expect = gen_data(in_shape, in_dtype, axis, keepdims)
    args = (data, output)
    output = utils.mod_launch(mod, args, expect=expect)
    res = np.allclose(output, expect, rtol=5e-03, atol=1.e-8)
    print("Test {}".format("Pass" if res else "Fail"))
    if not res:
        print("Error cuda:========================")
        print(mod.imported_modules[0].get_source())
        raise AssertionError("Test fail")
    data, expect = to_tvm_nd_array([data, expect])
    gpu_profiling(mod, data, expect, repeat_time=400)
