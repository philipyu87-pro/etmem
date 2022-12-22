# Copyright 2021 Huawei Technologies Co., Ltd
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

# shellcheck disable=SC2154,SC2034

BASEPATH=$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}" )" &> /dev/null && pwd )
ROOTDIR="$BASEPATH"
PROGRAM=$(basename "${BASH_SOURCE[0]:-$0}")

# git clone https://gitee.com/mindspore/mindspore.git
export PATH="$PATH:/usr/local/llvm/bin"
llvm-as --version
# bash build.sh -p mindspore/lib -j2 -S on
bash build.sh
