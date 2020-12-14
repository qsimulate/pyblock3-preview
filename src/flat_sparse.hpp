
/*
 * pyblock3: An Efficient python MPS/DMRG Library
 * Copyright (C) 2020 The pyblock3 developers. All Rights Reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "bond_info.hpp"
#include "tensor_impl.hpp"
#ifdef I
#undef I
#endif
#include <pybind11/numpy.h>
#include <tuple>

namespace py = pybind11;
using namespace std;

void flat_sparse_tensor_transpose(const py::array_t<uint32_t> &ashs,
                                  const py::array_t<double> &adata,
                                  const py::array_t<uint32_t> &aidxs,
                                  const py::array_t<int32_t> &perm,
                                  py::array_t<double> &cdata);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_tensordot(
    const py::array_t<uint32_t> &aqs, const py::array_t<uint32_t> &ashs,
    const py::array_t<double> &adata, const py::array_t<uint32_t> &aidxs,
    const py::array_t<uint32_t> &bqs, const py::array_t<uint32_t> &bshs,
    const py::array_t<double> &bdata, const py::array_t<uint32_t> &bidxs,
    const py::array_t<int> &idxa, const py::array_t<int> &idxb);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_add(
    const py::array_t<uint32_t> &aqs, const py::array_t<uint32_t> &ashs,
    const py::array_t<double> &adata, const py::array_t<uint32_t> &aidxs,
    const py::array_t<uint32_t> &bqs, const py::array_t<uint32_t> &bshs,
    const py::array_t<double> &bdata, const py::array_t<uint32_t> &bidxs);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_kron_add(
    const py::array_t<uint32_t> &aqs, const py::array_t<uint32_t> &ashs,
    const py::array_t<double> &adata, const py::array_t<uint32_t> &aidxs,
    const py::array_t<uint32_t> &bqs, const py::array_t<uint32_t> &bshs,
    const py::array_t<double> &bdata, const py::array_t<uint32_t> &bidxs,
    const unordered_map<uint32_t, uint32_t> &infol,
    const unordered_map<uint32_t, uint32_t> &infor);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_fuse(const py::array_t<uint32_t> &aqs,
                        const py::array_t<uint32_t> &ashs,
                        const py::array_t<double> &adata,
                        const py::array_t<uint32_t> &aidxs,
                        const py::array_t<int32_t> &idxs,
                        const map_fusing &info, const string &pattern);

map_fusing flat_sparse_tensor_kron_sum_info(const py::array_t<uint32_t> &aqs,
                                            const py::array_t<uint32_t> &ashs,
                                            const string &pattern);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>>
flat_sparse_tensor_skeleton(
    const vector<unordered_map<uint32_t, uint32_t>> &infos,
    const string &pattern, uint32_t fdq);

vector<unordered_map<uint32_t, uint32_t>>
flat_sparse_tensor_get_infos(const py::array_t<uint32_t> &aqs,
                             const py::array_t<uint32_t> &ashs);

enum DIRECTION { LEFT = 1, RIGHT = 0 };

template <DIRECTION L>
tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_canonicalize(const py::array_t<uint32_t> &aqs,
                         const py::array_t<uint32_t> &ashs,
                         const py::array_t<double> &adata,
                         const py::array_t<uint32_t> &aidxs, uint32_t *pxidx);

template <DIRECTION L>
tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_svd(const py::array_t<uint32_t> &aqs,
                const py::array_t<uint32_t> &ashs,
                const py::array_t<double> &adata,
                const py::array_t<uint32_t> &aidxs, uint32_t *pxidx);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_left_canonicalize(const py::array_t<uint32_t> &aqs,
                              const py::array_t<uint32_t> &ashs,
                              const py::array_t<double> &adata,
                              const py::array_t<uint32_t> &aidxs);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_right_canonicalize(const py::array_t<uint32_t> &aqs,
                               const py::array_t<uint32_t> &ashs,
                               const py::array_t<double> &adata,
                               const py::array_t<uint32_t> &aidxs);
tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_left_svd(const py::array_t<uint32_t> &aqs,
                     const py::array_t<uint32_t> &ashs,
                     const py::array_t<double> &adata,
                     const py::array_t<uint32_t> &aidxs);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_right_svd(const py::array_t<uint32_t> &aqs,
                      const py::array_t<uint32_t> &ashs,
                      const py::array_t<double> &adata,
                      const py::array_t<uint32_t> &aidxs);

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_left_canonicalize_indexed(const py::array_t<uint32_t> &aqs,
                                      const py::array_t<uint32_t> &ashs,
                                      const py::array_t<double> &adata,
                                      const py::array_t<uint32_t> &aidxs);

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_right_canonicalize_indexed(const py::array_t<uint32_t> &aqs,
                                       const py::array_t<uint32_t> &ashs,
                                       const py::array_t<double> &adata,
                                       const py::array_t<uint32_t> &aidxs);

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_left_svd_indexed(const py::array_t<uint32_t> &aqs,
                             const py::array_t<uint32_t> &ashs,
                             const py::array_t<double> &adata,
                             const py::array_t<uint32_t> &aidxs);

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_right_svd_indexed(const py::array_t<uint32_t> &aqs,
                              const py::array_t<uint32_t> &ashs,
                              const py::array_t<double> &adata,
                              const py::array_t<uint32_t> &aidxs);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_tensor_svd(const py::array_t<uint32_t> &aqs,
                       const py::array_t<uint32_t> &ashs,
                       const py::array_t<double> &adata,
                       const py::array_t<uint32_t> &aidxs, int idx,
                       const map_fusing &linfo, const map_fusing &rinfo,
                       const string &pattern);

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>, double>
flat_sparse_truncate_svd(
    const py::array_t<uint32_t> &lqs, const py::array_t<uint32_t> &lshs,
    const py::array_t<double> &ldata, const py::array_t<uint32_t> &lidxs,
    const py::array_t<uint32_t> &sqs, const py::array_t<uint32_t> &sshs,
    const py::array_t<double> &sdata, const py::array_t<uint32_t> &sidxs,
    const py::array_t<uint32_t> &rqs, const py::array_t<uint32_t> &rshs,
    const py::array_t<double> &rdata, const py::array_t<uint32_t> &ridxs,
    int max_bond_dim, double cutoff, double max_dw, double norm_cutoff,
    bool eigen_values);
