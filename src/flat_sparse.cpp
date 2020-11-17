
#include "flat_sparse.hpp"
#include <algorithm>
#include <cstring>
#include <numeric>

void flat_sparse_tensor_transpose(const py::array_t<uint32_t> &ashs,
                                  const py::array_t<double> &adata,
                                  const py::array_t<uint32_t> &aidxs,
                                  const py::array_t<int32_t> &perm,
                                  py::array_t<double> &cdata) {
    int n_blocks_a = (int)ashs.shape()[0], ndima = (int)ashs.shape()[1];
    const ssize_t asi = ashs.strides()[0] / sizeof(uint32_t),
                  asj = ashs.strides()[1] / sizeof(uint32_t);
    const int *perma = (const int *)perm.data();
    const double *pa = adata.data();
    const uint32_t *pia = aidxs.data(), *psha = ashs.data();
    double *pc = cdata.mutable_data();
    for (int ia = 0; ia < n_blocks_a; ia++) {
        const double *a = pa + pia[ia];
        double *c = pc + pia[ia];
        int shape_a[ndima];
        for (int i = 0; i < ndima; i++)
            shape_a[i] = psha[ia * asi + i * asj];
        uint32_t size_a = pia[ia + 1] - pia[ia];
        tensor_transpose_impl(ndima, size_a, perma, shape_a, a, c, 1.0, 0.0);
    }
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_tensordot(
    const py::array_t<uint32_t> &aqs, const py::array_t<uint32_t> &ashs,
    const py::array_t<double> &adata, const py::array_t<uint32_t> &aidxs,
    const py::array_t<uint32_t> &bqs, const py::array_t<uint32_t> &bshs,
    const py::array_t<double> &bdata, const py::array_t<uint32_t> &bidxs,
    const py::array_t<int> &idxa, const py::array_t<int> &idxb) {
    if (aqs.shape()[0] == 0)
        return std::make_tuple(aqs, ashs, adata, aidxs);
    else if (bqs.shape()[0] == 0)
        return std::make_tuple(bqs, bshs, bdata, bidxs);

    int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    int n_blocks_b = (int)bqs.shape()[0], ndimb = (int)bqs.shape()[1];
    int nctr = (int)idxa.shape()[0];
    int ndimc = ndima - nctr + ndimb - nctr;
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    const ssize_t bsi = bqs.strides()[0] / sizeof(uint32_t),
                  bsj = bqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(memcmp(bqs.strides(), bshs.strides(), 2 * sizeof(ssize_t)) == 0);

    // sort contracted indices (for tensor a)
    int pidxa[nctr], pidxb[nctr], ctr_idx[nctr];
    const int *ppidxa = idxa.data(), *ppidxb = idxb.data();
    for (int i = 0; i < nctr; i++)
        ctr_idx[i] = i;
    sort(ctr_idx, ctr_idx + nctr,
         [ppidxa](int a, int b) { return ppidxa[a] < ppidxa[b]; });
    for (int i = 0; i < nctr; i++)
        pidxa[i] = ppidxa[ctr_idx[i]], pidxb[i] = ppidxb[ctr_idx[i]];

    // checking whether permute is necessary
    int trans_a = 0, trans_b = 0;
    if (nctr == 0)
        trans_a = 1;
    else if (pidxa[nctr - 1] - pidxa[0] == nctr - 1) {
        if (pidxa[0] == 0 ||
            is_shape_one(ashs.data(), n_blocks_a, pidxa[0], asi, asj))
            trans_a = 1;
        else if (pidxa[nctr - 1] == ndima - 1 ||
                 is_shape_one(ashs.data() + (pidxa[nctr - 1] + 1) * asj,
                              n_blocks_a, ndima - (pidxa[nctr - 1] + 1), asi,
                              asj))
            trans_a = -1;
    }

    if (nctr == 0)
        trans_b = 1;
    else if (is_sorted(pidxb, pidxb + nctr) &&
             pidxb[nctr - 1] - pidxb[0] == nctr - 1) {
        if (pidxb[0] == 0 ||
            is_shape_one(bshs.data(), n_blocks_b, pidxb[0], bsi, bsj))
            trans_b = 1;
        else if (pidxb[nctr - 1] == ndimb - 1 ||
                 is_shape_one(bshs.data() + (pidxb[nctr - 1] + 1) * bsj,
                              n_blocks_b, ndimb - (pidxb[nctr - 1] + 1), bsi,
                              bsj))
            trans_b = -1;
    }

    // free indices
    int maska[ndima], maskb[ndimb], outa[ndima - nctr], outb[ndimb - nctr];
    memset(maska, -1, ndima * sizeof(int));
    memset(maskb, -1, ndimb * sizeof(int));
    for (int i = 0; i < nctr; i++)
        maska[pidxa[i]] = i, maskb[pidxb[i]] = i;
    for (int i = 0, j = 0; i < ndima; i++)
        if (maska[i] == -1)
            outa[j++] = i;
    for (int i = 0, j = 0; i < ndimb; i++)
        if (maskb[i] == -1)
            outb[j++] = i;

    // permutation
    vector<int> perma(ndima + n_blocks_a + 1, -1);
    vector<int> permb(ndimb + n_blocks_b + 1, -1);
    int *piatr = perma.data() + ndima, *pibtr = permb.data() + ndimb;
    if (trans_a == 0) {
        for (int i = 0; i < nctr; i++)
            perma[i] = pidxa[i];
        for (int i = nctr; i < ndima; i++)
            perma[i] = outa[i - nctr];
    }
    if (trans_b == 0) {
        for (int i = 0; i < nctr; i++)
            permb[i] = pidxb[i];
        for (int i = nctr; i < ndimb; i++)
            permb[i] = outb[i - nctr];
    }

    // free and contracted dims
    vector<int> a_free_dim(n_blocks_a), b_free_dim(n_blocks_b),
        ctr_dim(n_blocks_a);
    vector<vector<int>> a_free_dims(n_blocks_a), b_free_dims(n_blocks_b);
    const uint32_t *psh = ashs.data();
    for (int i = 0; i < n_blocks_a; i++) {
        a_free_dims[i].resize(ndima - nctr);
        a_free_dim[i] = ctr_dim[i] = 1;
        for (int j = 0; j < nctr; j++)
            ctr_dim[i] *= psh[i * asi + pidxa[j] * asj];
        for (int j = 0; j < ndima - nctr; j++)
            a_free_dim[i] *= (a_free_dims[i][j] = psh[i * asi + outa[j] * asj]);
    }
    psh = bshs.data();
    for (int i = 0; i < n_blocks_b; i++) {
        b_free_dims[i].resize(ndimb - nctr);
        b_free_dim[i] = 1;
        for (int j = 0; j < ndimb - nctr; j++)
            b_free_dim[i] *= (b_free_dims[i][j] = psh[i * bsi + outb[j] * bsj]);
    }

    // contracted q_label hashs
    vector<size_t> ctrqas(n_blocks_a), ctrqbs(n_blocks_b), outqas(n_blocks_a),
        outqbs(n_blocks_b);
    psh = aqs.data();
    for (int i = 0; i < n_blocks_a; i++) {
        ctrqas[i] = q_labels_hash(psh + i * asi, nctr, pidxa, asj);
        outqas[i] = q_labels_hash(psh + i * asi, ndima - nctr, outa, asj);
    }
    psh = bqs.data();
    for (int i = 0; i < n_blocks_b; i++) {
        ctrqbs[i] = q_labels_hash(psh + i * bsi, nctr, pidxb, bsj);
        outqbs[i] = q_labels_hash(psh + i * bsi, ndimb - nctr, outb, bsj);
    }

    unordered_map<size_t, vector<int>> map_idx_b;
    for (int i = 0; i < n_blocks_b; i++)
        map_idx_b[ctrqbs[i]].push_back(i);

    unordered_map<size_t,
                  vector<pair<vector<uint32_t>, vector<pair<int, int>>>>>
        map_out_q;
    ssize_t csize = 0;
    int n_blocks_c = 0;
    for (int ia = 0; ia < n_blocks_a; ia++) {
        if (map_idx_b.count(ctrqas[ia])) {
            piatr[ia] = 0;
            const auto &vb = map_idx_b.at(ctrqas[ia]);
            vector<uint32_t> q_out(ndimc);
            psh = aqs.data() + ia * asi;
            for (int i = 0; i < ndima - nctr; i++)
                q_out[i] = psh[outa[i] * asj];
            for (int ib : vb) {
                // check hashing conflicits
                bool same = true;
                for (int ict = 0; ict < nctr; ict++)
                    if (*aqs.data(ia, pidxa[ict]) !=
                        *bqs.data(ib, pidxb[ict])) {
                        same = false;
                        break;
                    }
                if (!same)
                    continue;
                pibtr[ib] = 0;
                size_t hout = outqas[ia];
                hout ^= outqbs[ib] + 0x9E3779B9 + (hout << 6) + (hout >> 2);
                psh = bqs.data() + ib * bsi;
                for (int i = 0; i < ndimb - nctr; i++)
                    q_out[i + ndima - nctr] = psh[outb[i] * bsj];
                int iq = 0;
                if (map_out_q.count(hout)) {
                    auto &vq = map_out_q.at(hout);
                    for (; iq < (int)vq.size() && q_out != vq[iq].first; iq++)
                        ;
                    if (iq == (int)vq.size()) {
                        vq.push_back(make_pair(
                            q_out, vector<pair<int, int>>{make_pair(ia, ib)}));
                        csize += (ssize_t)a_free_dim[ia] * b_free_dim[ib],
                            n_blocks_c++;
                    } else
                        vq[iq].second.push_back(make_pair(ia, ib));
                } else {
                    map_out_q[hout].push_back(make_pair(
                        q_out, vector<pair<int, int>>{make_pair(ia, ib)}));
                    csize += (ssize_t)a_free_dim[ia] * b_free_dim[ib],
                        n_blocks_c++;
                }
            }
        }
    }

    vector<ssize_t> sh = {n_blocks_c, ndimc};
    py::array_t<uint32_t> cqs(sh), cshs(sh),
        cidxs(vector<ssize_t>{n_blocks_c + 1});
    assert(cqs.strides()[1] == sizeof(uint32_t));
    assert(cshs.strides()[1] == sizeof(uint32_t));
    py::array_t<double> cdata(vector<ssize_t>{csize});
    uint32_t *pcqs = cqs.mutable_data(), *pcshs = cshs.mutable_data(),
             *pcidxs = cidxs.mutable_data();
    vector<uint32_t> psha(n_blocks_a * ndima), pshb(n_blocks_b * ndimb);
    for (int i = 0; i < n_blocks_a; i++)
        for (int j = 0; j < ndima; j++)
            psha[i * ndima + j] = ashs.data()[i * asi + j * asj];
    for (int i = 0; i < n_blocks_b; i++)
        for (int j = 0; j < ndimb; j++)
            pshb[i * ndimb + j] = bshs.data()[i * bsi + j * bsj];

    pcidxs[0] = 0;
    for (auto &mq : map_out_q) {
        for (auto &mmq : mq.second) {
            int xia = mmq.second[0].first, xib = mmq.second[0].second;
            memcpy(pcqs, mmq.first.data(), ndimc * sizeof(uint32_t));
            memcpy(pcshs, a_free_dims[xia].data(),
                   (ndima - nctr) * sizeof(uint32_t));
            memcpy(pcshs + (ndima - nctr), b_free_dims[xib].data(),
                   (ndimb - nctr) * sizeof(uint32_t));
            pcidxs[1] = pcidxs[0] + (uint32_t)a_free_dim[xia] * b_free_dim[xib];
            pcqs += ndimc;
            pcshs += ndimc;
            pcidxs++;
        }
    }

    // transpose
    double *pa = (double *)adata.data(), *pb = (double *)bdata.data();
    uint32_t *pia = (uint32_t *)aidxs.data(), *pib = (uint32_t *)bidxs.data();
    if (trans_a == 0) {
        int iatr = 0;
        for (int ia = 0; ia < n_blocks_a; ia++)
            if (piatr[ia] != -1)
                piatr[ia] = iatr, iatr += pia[ia + 1] - pia[ia];
        double *new_pa = new double[iatr];
        int *new_pia = (int *)piatr;
        for (int ia = 0; ia < n_blocks_a; ia++)
            if (piatr[ia] != -1) {
                double *a = pa + pia[ia], *new_a = new_pa + new_pia[ia];
                const int *shape_a = (const int *)(psha.data() + ia * ndima);
                uint32_t size_a = pia[ia + 1] - pia[ia];
                tensor_transpose_impl(ndima, size_a, perma.data(), shape_a, a,
                                      new_a, 1.0, 0.0);
            }
        trans_a = 1;
        pa = new_pa;
        pia = (uint32_t *)new_pia;
    }

    if (trans_b == 0) {
        int ibtr = 0;
        for (int ib = 0; ib < n_blocks_b; ib++)
            if (pibtr[ib] != -1)
                pibtr[ib] = ibtr, ibtr += pib[ib + 1] - pib[ib];
        double *new_pb = new double[ibtr];
        int *new_pib = (int *)pibtr;
        for (int ib = 0; ib < n_blocks_b; ib++)
            if (pibtr[ib] != -1) {
                double *b = pb + pib[ib], *new_b = new_pb + new_pib[ib];
                const int *shape_b = (const int *)(pshb.data() + ib * ndimb);
                uint32_t size_b = pib[ib + 1] - pib[ib];
                tensor_transpose_impl(ndimb, size_b, permb.data(), shape_b, b,
                                      new_b, 1.0, 0.0);
            }
        trans_b = 1;
        pb = new_pb;
        pib = (uint32_t *)new_pib;
    }

    auto tra = trans_a == -1 ? "n" : "t";
    auto trb = trans_b == 1 ? "n" : "t";
    const double alpha = 1.0, beta = 0.0;

    double *pc = cdata.mutable_data();
    for (auto &mq : map_out_q) {
        for (auto &mmq : mq.second) {
            int xia = 0, xib = 0;
            for (size_t i = 0; i < mmq.second.size(); i++) {
                xia = mmq.second[i].first, xib = mmq.second[i].second;
                int ldb = trans_b == 1 ? b_free_dim[xib] : ctr_dim[xia];
                int lda = trans_a == -1 ? ctr_dim[xia] : a_free_dim[xia];
                int ldc = b_free_dim[xib];
                dgemm(trb, tra, &b_free_dim[xib], &a_free_dim[xia],
                      &ctr_dim[xia], &alpha, pb + pib[xib], &ldb, pa + pia[xia],
                      &lda, i == 0 ? &beta : &alpha, pc, &ldc);
            }
            pc += (uint32_t)a_free_dim[xia] * b_free_dim[xib];
        }
    }

    if (pa != adata.data())
        delete[] pa;
    if (pb != bdata.data())
        delete[] pb;

    return std::make_tuple(cqs, cshs, cdata, cidxs);
}

inline void add_blocks(const int n_blocks_a, const int n_blocks_b,
                       const int ndim, const uint32_t *paqs,
                       const uint32_t *pbqs, const uint32_t *pia,
                       const uint32_t *pib, const ssize_t asi,
                       const ssize_t asj, const ssize_t bsi, const ssize_t bsj,
                       unordered_map<size_t, vector<int>> &map_idx, int *iab_mp,
                       int &n_blocks_c, ssize_t &csize) {
    // q_label hashs
    vector<size_t> hqas(n_blocks_a), hqbs(n_blocks_b);
    vector<uint32_t> qs(ndim * (n_blocks_a + n_blocks_b));
    for (int i = 0; i < n_blocks_a; i++) {
        uint32_t *pq = qs.data() + i * ndim;
        for (int j = 0; j < ndim; j++)
            pq[j] = paqs[i * asi + j * asj];
        hqas[i] = q_labels_hash(pq, ndim, 1);
    }
    for (int i = 0; i < n_blocks_b; i++) {
        uint32_t *pq = qs.data() + (i + n_blocks_a) * ndim;
        for (int j = 0; j < ndim; j++)
            pq[j] = pbqs[i * bsi + j * bsj];
        hqbs[i] = q_labels_hash(pq, ndim, 1);
    }

    csize = 0;
    n_blocks_c = 0;
    for (int i = 0; i < n_blocks_a; i++) {
        map_idx[hqas[i]].push_back(i);
        csize += (ssize_t)pia[i + 1] - pia[i];
        n_blocks_c++;
        iab_mp[i] = i;
    }
    for (int i = 0; i < n_blocks_b; i++)
        if (map_idx.count(hqbs[i]) == 0) {
            map_idx[hqbs[i]].push_back(i + n_blocks_a);
            csize += (ssize_t)pib[i + 1] - pib[i];
            n_blocks_c++;
            iab_mp[i + n_blocks_a] = i + n_blocks_a;
        } else {
            vector<int> vi(ndim);
            int iq = 0;
            auto &vq = map_idx.at(hqbs[i]);
            for (; iq < (int)vq.size() &&
                   memcmp(qs.data() + vq[iq] * ndim,
                          qs.data() + (i + n_blocks_a) * ndim,
                          sizeof(uint32_t) * ndim) != 0;
                 iq++)
                ;
            if (iq == (int)vq.size()) {
                vq.push_back(i + n_blocks_a);
                csize += (ssize_t)pib[i + 1] - pib[i];
                n_blocks_c++;
            }
            iab_mp[i + n_blocks_a] = vq[iq];
        }
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_add(
    const py::array_t<uint32_t> &aqs, const py::array_t<uint32_t> &ashs,
    const py::array_t<double> &adata, const py::array_t<uint32_t> &aidxs,
    const py::array_t<uint32_t> &bqs, const py::array_t<uint32_t> &bshs,
    const py::array_t<double> &bdata, const py::array_t<uint32_t> &bidxs) {

    const int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const int n_blocks_b = (int)bqs.shape()[0], ndimb = (int)bqs.shape()[1];
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    const ssize_t bsi = bqs.strides()[0] / sizeof(uint32_t),
                  bsj = bqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(memcmp(bqs.strides(), bshs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(ndima == ndimb);

    unordered_map<size_t, vector<int>> map_idx;
    const uint32_t *pia = aidxs.data(), *pib = bidxs.data();
    vector<int> iab_mp(n_blocks_a + n_blocks_b);
    ssize_t csize = 0;
    int n_blocks_c = 0;

    add_blocks(n_blocks_a, n_blocks_b, ndima, aqs.data(), bqs.data(), pia, pib,
               asi, asj, bsi, bsj, map_idx, iab_mp.data(), n_blocks_c, csize);

    vector<ssize_t> sh = {n_blocks_c, ndima};
    py::array_t<uint32_t> cqs(sh), cshs(sh),
        cidxs(vector<ssize_t>{n_blocks_c + 1});
    assert(cqs.strides()[1] == sizeof(uint32_t));
    assert(cshs.strides()[1] == sizeof(uint32_t));
    uint32_t *pcqs = cqs.mutable_data(), *pcshs = cshs.mutable_data(),
             *pcidxs = cidxs.mutable_data();
    int ic = 0;
    pcidxs[0] = 0;
    vector<int> pic(n_blocks_a + n_blocks_b);
    for (auto &p : map_idx) {
        for (int iab : p.second) {
            pic[iab] = ic;
            if (iab < n_blocks_a) {
                for (int j = 0; j < ndima; j++) {
                    pcqs[ic * ndima + j] = aqs.data()[iab * asi + j * asj];
                    pcshs[ic * ndima + j] = ashs.data()[iab * asi + j * asj];
                }
                pcidxs[ic + 1] =
                    pcidxs[ic] + ((uint32_t)pia[iab + 1] - pia[iab]);
            } else {
                iab -= n_blocks_a;
                for (int j = 0; j < ndimb; j++) {
                    pcqs[ic * ndima + j] = bqs.data()[iab * bsi + j * bsj];
                    pcshs[ic * ndima + j] = bshs.data()[iab * bsi + j * bsj];
                }
                pcidxs[ic + 1] =
                    pcidxs[ic] + ((uint32_t)pib[iab + 1] - pib[iab]);
            }
            ic++;
        }
    }

    py::array_t<double> cdata(vector<ssize_t>{csize});
    const double *pa = adata.data(), *pb = bdata.data();
    double *pc = cdata.mutable_data();
    const int inc = 1;
    const double alpha = 1.0;
    for (int i = 0; i < n_blocks_a; i++) {
        int iab = iab_mp[i], ic = pic[iab];
        int n = pcidxs[ic + 1] - pcidxs[ic];
        if (iab == i)
            dcopy(&n, pa + pia[i], &inc, pc + pcidxs[ic], &inc);
        else
            daxpy(&n, &alpha, pa + pia[i], &inc, pc + pcidxs[ic], &inc);
    }
    for (int i = 0; i < n_blocks_b; i++) {
        int iab = iab_mp[i + n_blocks_a], ic = pic[iab];
        int n = pcidxs[ic + 1] - pcidxs[ic];
        if (iab == i + n_blocks_a)
            dcopy(&n, pb + pib[i], &inc, pc + pcidxs[ic], &inc);
        else
            daxpy(&n, &alpha, pb + pib[i], &inc, pc + pcidxs[ic], &inc);
    }

    return std::make_tuple(cqs, cshs, cdata, cidxs);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_kron_add(
    const py::array_t<uint32_t> &aqs, const py::array_t<uint32_t> &ashs,
    const py::array_t<double> &adata, const py::array_t<uint32_t> &aidxs,
    const py::array_t<uint32_t> &bqs, const py::array_t<uint32_t> &bshs,
    const py::array_t<double> &bdata, const py::array_t<uint32_t> &bidxs,
    const unordered_map<uint32_t, uint32_t> &infol,
    const unordered_map<uint32_t, uint32_t> &infor) {
    if (aqs.shape()[0] == 0)
        return std::make_tuple(bqs, bshs, bdata, bidxs);
    else if (bqs.shape()[0] == 0)
        return std::make_tuple(aqs, ashs, adata, aidxs);
    const int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const int n_blocks_b = (int)bqs.shape()[0], ndimb = (int)bqs.shape()[1];
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    const ssize_t bsi = bqs.strides()[0] / sizeof(uint32_t),
                  bsj = bqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(memcmp(bqs.strides(), bshs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(ndima == ndimb);

    unordered_map<size_t, vector<int>> map_idx;
    const uint32_t *pia = aidxs.data(), *pib = bidxs.data();
    vector<int> iab_mp(n_blocks_a + n_blocks_b);
    ssize_t csize = 0;
    int n_blocks_c = 0;

    add_blocks(n_blocks_a, n_blocks_b, ndima, aqs.data(), bqs.data(), pia, pib,
               asi, asj, bsi, bsj, map_idx, iab_mp.data(), n_blocks_c, csize);

    vector<ssize_t> sh = {n_blocks_c, ndima};
    py::array_t<uint32_t> cqs(sh), cshs(sh),
        cidxs(vector<ssize_t>{n_blocks_c + 1});
    assert(cqs.strides()[1] == sizeof(uint32_t));
    assert(cshs.strides()[1] == sizeof(uint32_t));
    uint32_t *pcqs = cqs.mutable_data(), *pcshs = cshs.mutable_data(),
             *pcidxs = cidxs.mutable_data();
    int ic = 0;
    pcidxs[0] = 0;
    vector<int> pic(n_blocks_a + n_blocks_b);
    vector<uint32_t> dimlc(n_blocks_c), dimla(n_blocks_a), dimlb(n_blocks_b);
    for (int i = 0; i < n_blocks_a; i++) {
        dimla[i] = 1;
        for (int j = 0; j < ndima - 1; j++)
            dimla[i] *= ashs.data()[i * asi + j * asj];
    }
    for (int i = 0; i < n_blocks_b; i++) {
        dimlb[i] = 1;
        for (int j = 0; j < ndimb - 1; j++)
            dimlb[i] *= bshs.data()[i * bsi + j * bsj];
    }
    for (auto &p : map_idx) {
        for (int iab : p.second) {
            pic[iab] = ic;
            uint32_t *psh = pcshs + ic * ndima, *pq = pcqs + ic * ndima;
            if (iab < n_blocks_a) {
                for (int j = 0; j < ndima; j++) {
                    pq[j] = aqs.data()[iab * asi + j * asj];
                    psh[j] = ashs.data()[iab * asi + j * asj];
                }
                dimlc[ic] = dimla[iab] / psh[0];
            } else {
                iab -= n_blocks_a;
                for (int j = 0; j < ndimb; j++) {
                    pq[j] = bqs.data()[iab * bsi + j * bsj];
                    psh[j] = bshs.data()[iab * bsi + j * bsj];
                }
                dimlc[ic] = dimlb[iab] / psh[0];
            }
            psh[0] = infol.at(pq[0]);
            psh[ndima - 1] = infor.at(pq[ndima - 1]);
            dimlc[ic] *= psh[0];
            pcidxs[ic + 1] = pcidxs[ic] + dimlc[ic] * psh[ndima - 1];
            ic++;
        }
    }

    py::array_t<double> cdata(vector<ssize_t>{pcidxs[n_blocks_c]});
    const double *pa = adata.data(), *pb = bdata.data();
    double *pc = cdata.mutable_data();
    memset(pc, 0, sizeof(double) * cdata.size());
    for (int i = 0; i < n_blocks_a; i++) {
        int ic = pic[iab_mp[i]];
        const int lc = (int)dimlc[ic], la = (int)dimla[i];
        const int rc = (int)pcshs[ic * ndima + ndima - 1],
                  ra = (int)ashs.data()[i * asi + (ndima - 1) * asj];
        dlacpy("N", &ra, &la, pa + pia[i], &ra, pc + pcidxs[ic], &rc);
    }
    for (int i = 0; i < n_blocks_b; i++) {
        int ic = pic[iab_mp[i + n_blocks_a]];
        const int lc = (int)dimlc[ic], lb = (int)dimlb[i];
        const int rc = (int)pcshs[ic * ndima + ndima - 1],
                  rb = (int)bshs.data()[i * bsi + (ndimb - 1) * bsj];
        dlacpy("N", &rb, &lb, pb + pib[i], &rb,
               pc + pcidxs[ic] + (rc - rb) + (lc - lb) * rc, &rc);
    }

    return std::make_tuple(cqs, cshs, cdata, cidxs);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>>
flat_sparse_tensor_fuse(const py::array_t<uint32_t> &aqs,
                        const py::array_t<uint32_t> &ashs,
                        const py::array_t<double> &adata,
                        const py::array_t<uint32_t> &aidxs,
                        const py::array_t<int32_t> &idxs,
                        const map_fusing &info, const string &pattern) {
    if (aqs.shape()[0] == 0)
        return std::make_tuple(aqs, ashs, adata, aidxs);
    const int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    int nctr = idxs.size(), ndimc = ndima - nctr + 1;
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);

    const int32_t *pi = idxs.data();
    const uint32_t *pshs = ashs.data(), *pqs = aqs.data(), *pia = aidxs.data();
    vector<vector<uint32_t>> ufqs(n_blocks_a);
    vector<uint32_t> q_out(ndimc);

    assert(pi[nctr - 1] - pi[0] == nctr - 1 && is_sorted(pi, pi + nctr));
    unordered_map<size_t, vector<pair<vector<uint32_t>, vector<int>>>>
        map_out_q;
    ssize_t csize = 0;
    int n_blocks_c = 0;
    for (int ia = 0; ia < n_blocks_a; ia++) {
        // unfused size
        int nk = 1;
        SZ xq;
        ufqs[ia].resize(nctr);
        for (int j = 0; j < nctr; j++) {
            nk *= pshs[ia * asi + pi[j] * asj];
            ufqs[ia][j] = pqs[ia * asi + pi[j] * asj];
            xq = xq +
                 (pattern[j] == '+' ? to_sz(ufqs[ia][j]) : -to_sz(ufqs[ia][j]));
        }
        uint32_t xxq = from_sz(xq);
        if (info.count(xxq) == 0)
            continue;
        auto &m = info.at(xxq);
        auto &mm = m.second.at(ufqs[ia]);
        uint32_t x = m.first, k = mm.first;
        for (int j = 0; j < pi[0]; j++)
            q_out[j] = pqs[ia * asi + j * asj];
        q_out[pi[0]] = xxq;
        for (int j = pi[nctr - 1] + 1; j < ndima; j++)
            q_out[j - nctr + 1] = pqs[ia * asi + j * asj];
        size_t hout = q_labels_hash(q_out.data(), (int)q_out.size(), 1);
        int iq = 0;
        if (map_out_q.count(hout)) {
            auto &vq = map_out_q.at(hout);
            for (; iq < (int)vq.size() && q_out != vq[iq].first; iq++)
                ;
            if (iq == (int)vq.size()) {
                vq.push_back(make_pair(q_out, vector<int>{ia}));
                csize += (pia[ia + 1] - pia[ia]) / nk * x;
                n_blocks_c++;
            } else
                vq[iq].second.push_back(ia);
        } else {
            map_out_q[hout].push_back(make_pair(q_out, vector<int>{ia}));
            csize += (pia[ia + 1] - pia[ia]) / nk * x;
            n_blocks_c++;
        }
    }

    vector<ssize_t> sh = {n_blocks_c, ndimc};
    py::array_t<uint32_t> cqs(sh), cshs(sh),
        cidxs(vector<ssize_t>{n_blocks_c + 1});
    assert(cqs.strides()[1] == sizeof(uint32_t));
    assert(cshs.strides()[1] == sizeof(uint32_t));
    py::array_t<double> cdata(vector<ssize_t>{csize});
    uint32_t *pcqs = cqs.mutable_data(), *pcshs = cshs.mutable_data(),
             *pcidxs = cidxs.mutable_data();

    pcidxs[0] = 0;
    const double *pa = adata.data();
    double *pc = cdata.mutable_data();
    memset(pc, 0, sizeof(double) * csize);
    int ic = 0;
    for (auto &mq : map_out_q) {
        for (auto &mmq : mq.second) {
            int xia = mmq.second[0];
            memcpy(pcqs, mmq.first.data(), ndimc * sizeof(uint32_t));
            uint32_t xxq = mmq.first[pi[0]];
            auto &m = info.at(xxq);
            int diml = 1, dimr = 1;
            for (int j = 0; j < pi[0]; j++)
                diml *= (pcshs[j] = pshs[xia * asi + j * asj]);
            pcshs[pi[0]] = m.first;
            for (int j = pi[nctr - 1] + 1; j < ndima; j++)
                dimr *= (pcshs[j - nctr + 1] = pshs[xia * asi + j * asj]);
            uint32_t sz =
                accumulate(pcshs, pcshs + ndimc, 1, multiplies<uint32_t>());
            pcidxs[1] = pcidxs[0] + sz;
            for (auto &ia : mmq.second) {
                auto &mm = m.second.at(ufqs[ia]);
                uint32_t x = m.first, k = mm.first;
                int nk = (size_t)(pia[ia + 1] - pia[ia]) * x / sz;
                const int lc = diml, la = diml;
                const int rc = x * dimr, ra = nk * dimr;
                dlacpy("N", &ra, &la, pa + pia[ia], &ra,
                       pc + pcidxs[0] + k * dimr, &rc);
            }
            pcqs += ndimc;
            pcshs += ndimc;
            pcidxs++;
            ic++;
        }
    }

    assert(ic == n_blocks_c);

    return std::make_tuple(cqs, cshs, cdata, cidxs);
}

map_fusing flat_sparse_tensor_kron_sum_info(const py::array_t<uint32_t> &aqs,
                                            const py::array_t<uint32_t> &ashs,
                                            const string &pattern) {
    map_fusing r;
    if (aqs.shape()[0] == 0)
        return r;
    const int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);
    vector<SZ> qs(ndima);
    vector<uint32_t> shs(ndima);
    const uint32_t *pshs = ashs.data(), *pqs = aqs.data();
    unordered_map<uint32_t,
                  vector<pair<vector<SZ>, pair<uint32_t, vector<uint32_t>>>>>
        xr;
    for (int i = 0; i < n_blocks_a; i++) {
        SZ xq;
        for (int j = 0; j < ndima; j++) {
            qs[j] = to_sz(pqs[i * asi + j * asj]);
            shs[j] = pshs[i * asi + j * asj];
            xq = xq + (pattern[j] == '+' ? qs[j] : -qs[j]);
        }
        uint32_t xxq = from_sz(xq);
        uint32_t sz =
            accumulate(shs.begin(), shs.end(), 1, multiplies<uint32_t>());
        xr[xxq].push_back(make_pair(qs, make_pair(sz, shs)));
    }
    vector<uint32_t> xqs(ndima);
    for (auto &m : xr) {
        sort(m.second.begin(), m.second.end(),
             less_pvsz<pair<uint32_t, vector<uint32_t>>>);
        auto &mr = r[m.first];
        mr.first = 0;
        for (auto &mm : m.second) {
            bool same = mr.first == 0 ? false : true;
            for (int j = 0; j < ndima; j++) {
                uint32_t qq = from_sz(mm.first[j]);
                same = same && qq == xqs[j];
                xqs[j] = qq;
            }
            if (same)
                continue;
            mr.second[xqs] = make_pair(mr.first, mm.second.second);
            mr.first += mm.second.first;
        }
    }
    return r;
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>>
flat_sparse_tensor_skeleton(
    const vector<unordered_map<uint32_t, uint32_t>> &infos,
    const string &pattern, uint32_t fdq) {
    int ndim = (int)infos.size();
    size_t nx = 1;
    for (int i = 0; i < ndim; i++)
        nx *= infos[i].size();
    vector<uint32_t> qs, shs;
    vector<uint32_t> idxs(1, 0);
    vector<vector<pair<SZ, uint32_t>>> infox;
    bond_info_trans_to_sz(infos, pattern, infox, true);
    SZ dq = to_sz(fdq);
    vector<uint32_t> qk(ndim), shk(ndim);
    for (size_t x = 0; x < nx; x++) {
        size_t xp = x;
        SZ xq;
        for (int i = ndim - 1; i >= 0; xp /= infox[i].size(), i--)
            xq = xq + infox[i][xp % infox[i].size()].first;
        if (xq == dq || ndim == 1) {
            uint32_t sz = 1;
            xp = x;
            for (int i = ndim - 1; i >= 0; xp /= infox[i].size(), i--) {
                auto &r = infox[i][xp % infox[i].size()];
                qk[i] =
                    pattern[i] == '+' ? from_sz(r.first) : from_sz(-r.first),
                shk[i] = r.second, sz *= r.second;
            }
            qs.insert(qs.end(), qk.begin(), qk.end());
            shs.insert(shs.end(), shk.begin(), shk.end());
            idxs.push_back(idxs.back() + sz);
        }
    }
    vector<ssize_t> sh = {(ssize_t)qs.size() / ndim, ndim};
    py::array_t<uint32_t> cqs(sh), cshs(sh),
        cidxs(vector<ssize_t>{(ssize_t)idxs.size()});
    assert(cqs.strides()[1] == sizeof(uint32_t));
    assert(cshs.strides()[1] == sizeof(uint32_t));
    memcpy(cqs.mutable_data(), qs.data(), qs.size() * sizeof(uint32_t));
    memcpy(cshs.mutable_data(), shs.data(), shs.size() * sizeof(uint32_t));
    memcpy(cidxs.mutable_data(), idxs.data(), idxs.size() * sizeof(uint32_t));
    return std::make_tuple(cqs, cshs, cidxs);
}

vector<unordered_map<uint32_t, uint32_t>>
flat_sparse_tensor_get_infos(const py::array_t<uint32_t> &aqs,
                             const py::array_t<uint32_t> &ashs) {
    vector<unordered_map<uint32_t, uint32_t>> mqs;
    if (aqs.shape()[0] == 0)
        return mqs;
    int n = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);
    mqs.resize(ndima);
    for (int i = 0; i < ndima; i++)
        for (int j = 0; j < n; j++)
            mqs[i][aqs.data()[j * asi + i * asj]] =
                ashs.data()[j * asi + i * asj];
    return mqs;
}

template <DIRECTION L>
inline void collect_blocks(
    const unordered_map<uint32_t, vector<int>> &collected, int asi, int asj,
    int n_blocks_a, int ndima, int cidx, const uint32_t *paqs,
    const uint32_t *pashs, int &max_mshape, int &max_lshape, int &max_rshape,
    size_t &max_tmp_size, uint32_t *pqqs, uint32_t *pqshs, uint32_t *pqidxs,
    uint32_t *plrqs, uint32_t *plrshs, uint32_t *plridxs, uint32_t *psqs,
    uint32_t *psshs, uint32_t *psidxs, uint32_t *pxidx) {

    int ilr = 0, iq = 0;
    pqidxs[0] = 0;
    plridxs[0] = 0;
    if (psqs != nullptr)
        psidxs[0] = 0;
    uint32_t qblock_size[n_blocks_a];

    for (auto &cr : collected) {
        int nq = (int)cr.second.size();
        uint32_t lshape = 0, rshape = 0;
        int iqx = iq;
        for (int i : cr.second) {
            if (pxidx != nullptr)
                pxidx[iqx] = i;
            for (int j = 0; j < ndima; j++) {
                pqqs[iqx * ndima + j] = paqs[i * asi + j * asj];
                pqshs[iqx * ndima + j] = pashs[i * asi + j * asj];
            }
            if (L == LEFT) {
                qblock_size[iqx] = accumulate(pqshs + iqx * ndima,
                                              pqshs + iqx * ndima + (ndima - 1),
                                              1, multiplies<uint32_t>());
                lshape += qblock_size[iqx];
                rshape = pqshs[iqx * ndima + ndima - 1];
            } else {
                qblock_size[iqx] = accumulate(pqshs + iqx * ndima + 1,
                                              pqshs + iqx * ndima + ndima, 1,
                                              multiplies<uint32_t>());
                lshape = pqshs[iqx * ndima + 0];
                rshape += qblock_size[iqx];
            }
            iqx++;
        }
        uint32_t mshape = min(lshape, rshape);
        max_mshape = max((int)mshape, max_mshape);
        max_lshape = max((int)lshape, max_lshape);
        max_rshape = max((int)rshape, max_rshape);
        max_tmp_size = max((size_t)lshape * rshape, max_tmp_size);
        plrqs[ilr * 2 + 0] = plrqs[ilr * 2 + 1] = cr.first;
        plrshs[ilr * 2 + 0] = L == LEFT ? mshape : lshape;
        plrshs[ilr * 2 + 1] = L == LEFT ? rshape : mshape;
        if (psqs != nullptr)
            psqs[ilr] = cr.first, psshs[ilr] = mshape,
            psidxs[ilr + 1] = psidxs[ilr] + mshape;
        for (int i = 0; i < nq; i++) {
            qblock_size[iq + i] *= mshape;
            pqshs[(iq + i) * ndima + cidx] = mshape;
            pqidxs[iq + i + 1] = pqidxs[iq + i] + qblock_size[iq + i];
        }
        plridxs[ilr + 1] =
            plridxs[ilr] + plrshs[ilr * 2 + 0] * plrshs[ilr * 2 + 1];
        ilr++, iq += nq;
    }
}

template <DIRECTION L>
tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_canonicalize(const py::array_t<uint32_t> &aqs,
                         const py::array_t<uint32_t> &ashs,
                         const py::array_t<double> &adata,
                         const py::array_t<uint32_t> &aidxs, uint32_t *pxidx) {
    if (aqs.shape()[0] == 0)
        return std::make_tuple(aqs, ashs, adata, aidxs, aqs, ashs, adata,
                               aidxs);
    int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const int cidx = L == LEFT ? ndima - 1 : 0;
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);

    unordered_map<uint32_t, vector<int>> collected;
    const uint32_t *pashs = ashs.data(), *paqs = aqs.data(),
                   *pia = aidxs.data();
    const double *pa = adata.data();
    for (int i = 0; i < n_blocks_a; i++)
        collected[paqs[i * asi + cidx * asj]].push_back(i);

    int n_blocks_lr = collected.size();
    py::array_t<uint32_t> qqs(vector<ssize_t>{n_blocks_a, ndima}),
        qshs(vector<ssize_t>{n_blocks_a, ndima});
    py::array_t<uint32_t> qidxs(vector<ssize_t>{n_blocks_a + 1});
    py::array_t<uint32_t> lrqs(vector<ssize_t>{n_blocks_lr, 2}),
        lrshs(vector<ssize_t>{n_blocks_lr, 2});
    py::array_t<uint32_t> lridxs(vector<ssize_t>{n_blocks_lr + 1});

    uint32_t *pqqs = qqs.mutable_data(), *pqshs = qshs.mutable_data();
    uint32_t *plrqs = lrqs.mutable_data(), *plrshs = lrshs.mutable_data();
    uint32_t *pqidxs = qidxs.mutable_data(), *plridxs = lridxs.mutable_data();
    int max_mshape = 0, max_lshape = 0, max_rshape = 0;
    size_t max_tmp_size = 0;
    collect_blocks<L>(collected, asi, asj, n_blocks_a, ndima, cidx, paqs, pashs,
                      max_mshape, max_lshape, max_rshape, max_tmp_size, pqqs,
                      pqshs, pqidxs, plrqs, plrshs, plridxs, nullptr, nullptr,
                      nullptr, pxidx);

    py::array_t<double> qdata(vector<ssize_t>{pqidxs[n_blocks_a]});
    py::array_t<double> lrdata(vector<ssize_t>{plridxs[n_blocks_lr]});
    double *pq = qdata.mutable_data(), *plr = lrdata.mutable_data();
    memset(plr, 0, sizeof(double) * plridxs[n_blocks_lr]);
    int iq = 0, ilr = 0;
    int lwork = (L == LEFT ? max_rshape : max_lshape) * 34, info = 0;
    vector<double> tau(max_mshape), work(lwork), tmp(max_tmp_size);

    for (auto &cr : collected) {
        int nq = (int)cr.second.size(), lshape, rshape;
        if (L == LEFT) {
            lshape = (pqidxs[iq + nq] - pqidxs[iq]) / plrshs[ilr * 2 + 0];
            rshape = pashs[cr.second[0] * asi + cidx * asj];
        } else {
            lshape = pashs[cr.second[0] * asi + cidx * asj];
            rshape = (pqidxs[iq + nq] - pqidxs[iq]) / plrshs[ilr * 2 + 1];
        }
        int mshape = min(lshape, rshape);
        double *ptmp = tmp.data();
        if (L == LEFT) {
            for (int i = 0; i < nq; i++) {
                uint32_t ia = cr.second[i], sz = pia[ia + 1] - pia[ia];
                memcpy(ptmp, pa + pia[ia], sizeof(double) * sz);
                ptmp += sz;
            }
            dgelqf(&rshape, &lshape, tmp.data(), &rshape, tau.data(),
                   work.data(), &lwork, &info);
            assert(info == 0);
            for (int j = 0; j < mshape; j++)
                memcpy(plr + plridxs[ilr] + j * rshape + j,
                       tmp.data() + j * rshape + j,
                       sizeof(double) * (rshape - j));
            dorglq(&mshape, &lshape, &mshape, tmp.data(), &rshape, tau.data(),
                   work.data(), &lwork, &info);
            assert(info == 0);
            for (int j = 0; j < lshape; j++)
                memcpy(pq + pqidxs[iq] + j * mshape, tmp.data() + j * rshape,
                       sizeof(double) * mshape);
        } else {
            for (int i = 0; i < nq; i++) {
                int ia = cr.second[i], ra = (pia[ia + 1] - pia[ia]) / lshape;
                dlacpy("N", &ra, &lshape, pa + pia[ia], &ra, ptmp, &rshape);
                ptmp += ra;
            }
            dgeqrf(&rshape, &lshape, tmp.data(), &rshape, tau.data(),
                   work.data(), &lwork, &info);
            assert(info == 0);
            for (int j = 0; j < lshape; j++)
                memcpy(plr + plridxs[ilr] + j * mshape, tmp.data() + j * rshape,
                       sizeof(double) * min(mshape, j + 1));
            dorgqr(&rshape, &mshape, &mshape, tmp.data(), &rshape, tau.data(),
                   work.data(), &lwork, &info);
            assert(info == 0);
            ptmp = tmp.data();
            for (int i = 0; i < nq; i++) {
                int ia = cr.second[i], ra = (pia[ia + 1] - pia[ia]) / lshape;
                dlacpy("N", &ra, &mshape, ptmp, &rshape, pq + pqidxs[iq + i],
                       &ra);
                ptmp += ra;
            }
        }
        ilr++, iq += nq;
    }

    return L == LEFT ? std::make_tuple(qqs, qshs, qdata, qidxs, lrqs, lrshs,
                                       lrdata, lridxs)
                     : std::make_tuple(lrqs, lrshs, lrdata, lridxs, qqs, qshs,
                                       qdata, qidxs);
}

template <DIRECTION L>
tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_svd(const py::array_t<uint32_t> &aqs,
                const py::array_t<uint32_t> &ashs,
                const py::array_t<double> &adata,
                const py::array_t<uint32_t> &aidxs, uint32_t *pxidx) {
    if (aqs.shape()[0] == 0)
        return std::make_tuple(aqs, ashs, adata, aidxs, aqs, ashs, adata, aidxs,
                               aqs, ashs, adata, aidxs);
    int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const int cidx = L == LEFT ? ndima - 1 : 0;
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);

    unordered_map<uint32_t, vector<int>> collected;
    const uint32_t *pashs = ashs.data(), *paqs = aqs.data(),
                   *pia = aidxs.data();
    const double *pa = adata.data();
    for (int i = 0; i < n_blocks_a; i++)
        collected[paqs[i * asi + cidx * asj]].push_back(i);

    int n_blocks_lr = collected.size();
    py::array_t<uint32_t> qqs(vector<ssize_t>{n_blocks_a, ndima}),
        qshs(vector<ssize_t>{n_blocks_a, ndima});
    py::array_t<uint32_t> qidxs(vector<ssize_t>{n_blocks_a + 1});
    py::array_t<uint32_t> lrqs(vector<ssize_t>{n_blocks_lr, 2}),
        lrshs(vector<ssize_t>{n_blocks_lr, 2});
    py::array_t<uint32_t> lridxs(vector<ssize_t>{n_blocks_lr + 1});
    py::array_t<uint32_t> sqs(vector<ssize_t>{n_blocks_lr, 1}),
        sshs(vector<ssize_t>{n_blocks_lr, 1});
    py::array_t<uint32_t> sidxs(vector<ssize_t>{n_blocks_lr + 1});

    uint32_t *pqqs = qqs.mutable_data(), *pqshs = qshs.mutable_data(),
             *pqidxs = qidxs.mutable_data();
    uint32_t *plrqs = lrqs.mutable_data(), *plrshs = lrshs.mutable_data(),
             *plridxs = lridxs.mutable_data();
    uint32_t *psqs = sqs.mutable_data(), *psshs = sshs.mutable_data(),
             *psidxs = sidxs.mutable_data();
    int max_mshape = 0, max_lshape = 0, max_rshape = 0;
    size_t max_tmp_size = 0;
    collect_blocks<L>(collected, asi, asj, n_blocks_a, ndima, cidx, paqs, pashs,
                      max_mshape, max_lshape, max_rshape, max_tmp_size, pqqs,
                      pqshs, pqidxs, plrqs, plrshs, plridxs, psqs, psshs,
                      psidxs, pxidx);

    py::array_t<double> qdata(vector<ssize_t>{pqidxs[n_blocks_a]});
    py::array_t<double> lrdata(vector<ssize_t>{plridxs[n_blocks_lr]});
    py::array_t<double> sdata(vector<ssize_t>{psidxs[n_blocks_lr]});
    double *pq = qdata.mutable_data(), *plr = lrdata.mutable_data(),
           *ps = sdata.mutable_data();
    memset(plr, 0, sizeof(double) * plridxs[n_blocks_lr]);
    int iq = 0, ilr = 0;
    int lwork = max(max_lshape, max_rshape) * 34, info = 0;
    vector<double> work(lwork), tmp(max_tmp_size),
        tmp2(L == LEFT ? 0 : max_tmp_size);

    for (auto &cr : collected) {
        int nq = (int)cr.second.size(), lshape, rshape;
        if (L == LEFT) {
            lshape = (pqidxs[iq + nq] - pqidxs[iq]) / plrshs[ilr * 2 + 0];
            rshape = pashs[cr.second[0] * asi + cidx * asj];
        } else {
            lshape = pashs[cr.second[0] * asi + cidx * asj];
            rshape = (pqidxs[iq + nq] - pqidxs[iq]) / plrshs[ilr * 2 + 1];
        }
        int mshape = min(lshape, rshape);
        double *ptmp = tmp.data();
        if (L == LEFT) {
            for (int i = 0; i < nq; i++) {
                uint32_t ia = cr.second[i], sz = pia[ia + 1] - pia[ia];
                memcpy(ptmp, pa + pia[ia], sizeof(double) * sz);
                ptmp += sz;
            }
            dgesvd("S", "S", &rshape, &lshape, tmp.data(), &rshape,
                   ps + psidxs[ilr], plr + plridxs[ilr], &rshape,
                   pq + pqidxs[iq], &mshape, work.data(), &lwork, &info);
            assert(info == 0);
        } else {
            for (int i = 0; i < nq; i++) {
                int ia = cr.second[i], ra = (pia[ia + 1] - pia[ia]) / lshape;
                dlacpy("N", &ra, &lshape, pa + pia[ia], &ra, ptmp, &rshape);
                ptmp += ra;
            }
            dgesvd("S", "S", &rshape, &lshape, tmp.data(), &rshape,
                   ps + psidxs[ilr], tmp2.data(), &rshape, plr + plridxs[ilr],
                   &mshape, work.data(), &lwork, &info);
            assert(info == 0);
            ptmp = tmp2.data();
            for (int i = 0; i < nq; i++) {
                int ia = cr.second[i], ra = (pia[ia + 1] - pia[ia]) / lshape;
                dlacpy("N", &ra, &mshape, ptmp, &rshape, pq + pqidxs[iq + i],
                       &ra);
                ptmp += ra;
            }
        }
        ilr++, iq += nq;
    }

    return L == LEFT
               ? std::make_tuple(qqs, qshs, qdata, qidxs, sqs, sshs, sdata,
                                 sidxs, lrqs, lrshs, lrdata, lridxs)
               : std::make_tuple(lrqs, lrshs, lrdata, lridxs, sqs, sshs, sdata,
                                 sidxs, qqs, qshs, qdata, qidxs);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_left_canonicalize(const py::array_t<uint32_t> &aqs,
                              const py::array_t<uint32_t> &ashs,
                              const py::array_t<double> &adata,
                              const py::array_t<uint32_t> &aidxs) {
    return flat_sparse_canonicalize<LEFT>(aqs, ashs, adata, aidxs, nullptr);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_right_canonicalize(const py::array_t<uint32_t> &aqs,
                               const py::array_t<uint32_t> &ashs,
                               const py::array_t<double> &adata,
                               const py::array_t<uint32_t> &aidxs) {
    return flat_sparse_canonicalize<RIGHT>(aqs, ashs, adata, aidxs, nullptr);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_left_svd(const py::array_t<uint32_t> &aqs,
                     const py::array_t<uint32_t> &ashs,
                     const py::array_t<double> &adata,
                     const py::array_t<uint32_t> &aidxs) {
    return flat_sparse_svd<LEFT>(aqs, ashs, adata, aidxs, nullptr);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_right_svd(const py::array_t<uint32_t> &aqs,
                      const py::array_t<uint32_t> &ashs,
                      const py::array_t<double> &adata,
                      const py::array_t<uint32_t> &aidxs) {
    return flat_sparse_svd<RIGHT>(aqs, ashs, adata, aidxs, nullptr);
}

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_left_canonicalize_indexed(const py::array_t<uint32_t> &aqs,
                                      const py::array_t<uint32_t> &ashs,
                                      const py::array_t<double> &adata,
                                      const py::array_t<uint32_t> &aidxs) {
    py::array_t<uint32_t> xidx(vector<ssize_t>{aqs.shape()[0]});
    const auto &r = flat_sparse_canonicalize<LEFT>(aqs, ashs, adata, aidxs,
                                                   xidx.mutable_data());
    return make_pair(r, xidx);
}

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_right_canonicalize_indexed(const py::array_t<uint32_t> &aqs,
                                       const py::array_t<uint32_t> &ashs,
                                       const py::array_t<double> &adata,
                                       const py::array_t<uint32_t> &aidxs) {
    py::array_t<uint32_t> xidx(vector<ssize_t>{aqs.shape()[0]});
    const auto &r = flat_sparse_canonicalize<RIGHT>(aqs, ashs, adata, aidxs,
                                                    xidx.mutable_data());
    return make_pair(r, xidx);
}

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_left_svd_indexed(const py::array_t<uint32_t> &aqs,
                             const py::array_t<uint32_t> &ashs,
                             const py::array_t<double> &adata,
                             const py::array_t<uint32_t> &aidxs) {
    py::array_t<uint32_t> xidx(vector<ssize_t>{aqs.shape()[0]});
    const auto &r =
        flat_sparse_svd<LEFT>(aqs, ashs, adata, aidxs, xidx.mutable_data());
    return make_pair(r, xidx);
}

pair<tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
           py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
           py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>,
     py::array_t<uint32_t>>
flat_sparse_right_svd_indexed(const py::array_t<uint32_t> &aqs,
                              const py::array_t<uint32_t> &ashs,
                              const py::array_t<double> &adata,
                              const py::array_t<uint32_t> &aidxs) {
    py::array_t<uint32_t> xidx(vector<ssize_t>{aqs.shape()[0]});
    const auto &r =
        flat_sparse_svd<RIGHT>(aqs, ashs, adata, aidxs, xidx.mutable_data());
    return make_pair(r, xidx);
}

tuple<py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<double>,
      py::array_t<uint32_t>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<double>, py::array_t<uint32_t>, py::array_t<uint32_t>,
      py::array_t<uint32_t>, py::array_t<double>, py::array_t<uint32_t>>
flat_sparse_tensor_svd(const py::array_t<uint32_t> &aqs,
                       const py::array_t<uint32_t> &ashs,
                       const py::array_t<double> &adata,
                       const py::array_t<uint32_t> &aidxs, int idx,
                       const map_fusing &linfo, const map_fusing &rinfo,
                       const string &pattern) {
    if (aqs.shape()[0] == 0)
        return std::make_tuple(aqs, ashs, adata, aidxs, aqs, ashs, adata, aidxs,
                               aqs, ashs, adata, aidxs);
    const int n_blocks_a = (int)aqs.shape()[0], ndima = (int)aqs.shape()[1];
    const ssize_t asi = aqs.strides()[0] / sizeof(uint32_t),
                  asj = aqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(aqs.strides(), ashs.strides(), 2 * sizeof(ssize_t)) == 0);
    vector<vector<uint32_t>> ufqsl(n_blocks_a), ufqsr(n_blocks_a);
    vector<uint32_t> fqs(n_blocks_a);
    unordered_map<uint32_t, size_t> mat_mp;
    const uint32_t *pshs = ashs.data(), *pqs = aqs.data(), *pia = aidxs.data();
    const double *pa = adata.data();
    size_t s_size = 0, mat_size = 0;
    unordered_map<uint32_t, vector<int>> mat_idxl, mat_idxr;
    int max_lshape = 0, max_rshape = 0, max_tmpl_shape = 0, max_tmpr_shape = 0;
    for (int ia = 0; ia < n_blocks_a; ia++) {
        SZ xql, xqr;
        ufqsl[ia].resize(idx);
        ufqsr[ia].resize(ndima - idx);
        for (int j = 0; j < idx; j++) {
            ufqsl[ia][j] = pqs[ia * asi + j * asj];
            xql = xql + (pattern[j] == '+' ? to_sz(ufqsl[ia][j])
                                           : -to_sz(ufqsl[ia][j]));
        }
        for (int j = idx; j < ndima; j++) {
            ufqsr[ia][j - idx] = pqs[ia * asi + j * asj];
            xqr = xqr + (pattern[j] == '+' ? to_sz(ufqsr[ia][j - idx])
                                           : -to_sz(ufqsr[ia][j - idx]));
        }
        assert(xql == xqr);
        uint32_t q = from_sz(xql);
        fqs[ia] = q;
        if (!linfo.count(q) || !rinfo.count(q))
            continue;
        mat_idxl[q].push_back(ia);
        mat_idxr[q].push_back(ia);
        if (!mat_mp.count(q)) {
            mat_mp[q] = mat_size;
            int ml = linfo.at(q).first, mr = rinfo.at(q).first;
            mat_size += (size_t)ml * mr, s_size += min(ml, mr);
            max_lshape = max(ml, max_lshape);
            max_rshape = max(mr, max_rshape);
            max_tmpl_shape = max(min(ml, mr) * ml, max_tmpl_shape);
            max_tmpr_shape = max(min(ml, mr) * mr, max_tmpr_shape);
        }
    }

    vector<double> mat_data(mat_size);
    vector<pair<int, int>> lkns(n_blocks_a), rkns(n_blocks_a);
    for (int ia = 0; ia < n_blocks_a; ia++) {
        const uint32_t q = fqs[ia];
        const vector<uint32_t> &qls = ufqsl[ia], &qrs = ufqsr[ia];
        if (!mat_mp.count(q))
            continue;
        int lfn = linfo.at(q).first, rfn = rinfo.at(q).first;
        int mfn = min(lfn, rfn);
        const pair<uint32_t, vector<uint32_t>> &lpk =
            linfo.at(q).second.at(qls);
        const pair<uint32_t, vector<uint32_t>> &rpk =
            rinfo.at(q).second.at(qrs);
        int lk = (int)lpk.first, rk = (int)rpk.first;
        int lkn = (int)accumulate(lpk.second.begin(), lpk.second.end(), 1,
                                  multiplies<uint32_t>());
        int rkn = (int)accumulate(rpk.second.begin(), rpk.second.end(), 1,
                                  multiplies<uint32_t>());
        dlacpy("N", &rkn, &lkn, pa + pia[ia], &rkn,
               mat_data.data() + mat_mp[q] + rk + (size_t)lk * rfn, &rfn);
        lkns[ia] = make_pair(lk, lkn), rkns[ia] = make_pair(rk, rkn);
    }

    int n_blocks_s = (int)mat_mp.size();
    int n_blocks_l = 0, n_blocks_r = 0;
    size_t l_size = 0, r_size = 0;
    for (auto &v : mat_idxl) {
        int mm = min(linfo.at(v.first).first, rinfo.at(v.first).first);
        sort(v.second.begin(), v.second.end(),
             [&lkns](int i, int j) { return lkns[i].first < lkns[j].first; });
        for (size_t i = 1; i < v.second.size(); i++) {
            const int vi = v.second[i], vip = v.second[i - 1];
            if (lkns[vi].first == lkns[vip].first)
                v.second[i - 1] = -1;
        }
        for (auto &vi : v.second)
            if (vi != -1)
                l_size += (size_t)mm * lkns[vi].second, n_blocks_l++;
    }
    for (auto &v : mat_idxr) {
        int mm = min(linfo.at(v.first).first, rinfo.at(v.first).first);
        sort(v.second.begin(), v.second.end(),
             [&rkns](int i, int j) { return rkns[i].first < rkns[j].first; });
        for (size_t i = 1; i < v.second.size(); i++) {
            const int vi = v.second[i], vip = v.second[i - 1];
            if (rkns[vi].first == rkns[vip].first)
                v.second[i - 1] = -1;
        }
        for (auto &vi : v.second)
            if (vi != -1)
                r_size += (size_t)mm * rkns[vi].second, n_blocks_r++;
    }

    py::array_t<uint32_t> lqs(vector<ssize_t>{n_blocks_l, idx + 1}),
        lshs(vector<ssize_t>{n_blocks_l, idx + 1});
    py::array_t<uint32_t> lidxs(vector<ssize_t>{n_blocks_l + 1});
    py::array_t<uint32_t> rqs(vector<ssize_t>{n_blocks_r, ndima - idx + 1}),
        rshs(vector<ssize_t>{n_blocks_r, ndima - idx + 1});
    py::array_t<uint32_t> ridxs(vector<ssize_t>{n_blocks_r + 1});
    py::array_t<uint32_t> sqs(vector<ssize_t>{n_blocks_s, 1}),
        sshs(vector<ssize_t>{n_blocks_s, 1});
    py::array_t<uint32_t> sidxs(vector<ssize_t>{n_blocks_s + 1});
    py::array_t<double> ldata(vector<ssize_t>{(ssize_t)l_size});
    py::array_t<double> rdata(vector<ssize_t>{(ssize_t)r_size});
    py::array_t<double> sdata(vector<ssize_t>{(ssize_t)s_size});

    uint32_t *plqs = lqs.mutable_data(), *plshs = lshs.mutable_data(),
             *plidxs = lidxs.mutable_data();
    uint32_t *prqs = rqs.mutable_data(), *prshs = rshs.mutable_data(),
             *pridxs = ridxs.mutable_data();
    uint32_t *psqs = sqs.mutable_data(), *psshs = sshs.mutable_data(),
             *psidxs = sidxs.mutable_data();
    double *pl = ldata.mutable_data(), *pr = rdata.mutable_data(),
           *ps = sdata.mutable_data();
    int lwork = max(max_lshape, max_rshape) * 34, info = 0;
    vector<double> work(lwork), tmpl(max_tmpl_shape), tmpr(max_tmpr_shape);
    int iis = 0, iil = 0, iir = 0;
    psidxs[0] = plidxs[0] = pridxs[0] = 0;
    for (auto &mq : mat_mp) {
        const uint32_t q = mq.first;
        double *mat = mat_data.data() + mq.second;
        int ml = linfo.at(q).first, mr = rinfo.at(q).first, mm = min(ml, mr);
        int lwork = max(ml, mr) * 34;
        psidxs[iis + 1] = psidxs[iis] + mm;
        psqs[iis] = q, psshs[iis] = mm;
        dgesvd("S", "S", &mr, &ml, mat, &mr, ps + psidxs[iis], tmpr.data(), &mr,
               tmpl.data(), &mm, work.data(), &lwork, &info);
        assert(info == 0);
        int isl = 0, isr = 0;
        for (auto &v : mat_idxl[q]) {
            if (v == -1)
                continue;
            plidxs[iil + isl + 1] =
                plidxs[iil + isl] + (uint32_t)mm * lkns[v].second;
            dlacpy("N", &mm, &lkns[v].second, tmpl.data() + lkns[v].first * mm,
                   &mm, pl + plidxs[iil + isl], &mm);
            for (int i = 0; i < idx; i++) {
                plqs[(iil + isl) * (idx + 1) + i] = pqs[v * asi + i * asj];
                plshs[(iil + isl) * (idx + 1) + i] = pshs[v * asi + i * asj];
            }
            plqs[(iil + isl) * (idx + 1) + idx] = q;
            plshs[(iil + isl) * (idx + 1) + idx] = mm;
            isl++;
        }
        for (auto &v : mat_idxr[q]) {
            if (v == -1)
                continue;
            pridxs[iir + isr + 1] =
                pridxs[iir + isr] + (uint32_t)mm * rkns[v].second;
            dlacpy("N", &rkns[v].second, &mm, tmpr.data() + rkns[v].first, &mr,
                   pr + pridxs[iir + isr], &rkns[v].second);
            for (int i = idx; i < ndima; i++) {
                prqs[(iir + isr) * (ndima - idx + 1) + i - idx + 1] =
                    pqs[v * asi + i * asj];
                prshs[(iir + isr) * (ndima - idx + 1) + i - idx + 1] =
                    pshs[v * asi + i * asj];
            }
            prqs[(iir + isr) * (ndima - idx + 1) + 0] = q;
            prshs[(iir + isr) * (ndima - idx + 1) + 0] = mm;
            isr++;
        }
        iis++, iil += isl, iir += isr;
    }
    assert(iis == n_blocks_s && iil == n_blocks_l && iir == n_blocks_r);
    assert(psidxs[iis] == s_size && plidxs[iil] == l_size &&
           pridxs[iir] == r_size);
    return std::make_tuple(lqs, lshs, ldata, lidxs, sqs, sshs, sdata, sidxs,
                           rqs, rshs, rdata, ridxs);
}

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
    bool eigen_values) {
    if (sqs.shape()[0] == 0)
        return std::make_tuple(lqs, lshs, ldata, lidxs, sqs, sshs, sdata, sidxs,
                               sqs, sshs, sdata, sidxs, 0.0);
    int n_blocks_l = (int)lqs.shape()[0], ndiml = (int)lqs.shape()[1];
    int n_blocks_s = (int)sqs.shape()[0], ndims = (int)sqs.shape()[1];
    int n_blocks_r = (int)rqs.shape()[0], ndimr = (int)rqs.shape()[1];

    assert(ndims == 1);
    vector<tuple<int, int, double>> ss;
    ss.reserve(sidxs.data()[n_blocks_s]);
    const uint32_t *plqs = lqs.data(), *psqs = sqs.data(), *prqs = rqs.data();
    const uint32_t *plshs = lshs.data(), *psshs = sshs.data(),
                   *prshs = rshs.data();
    const double *pl = ldata.data(), *ps = sdata.data(), *pr = rdata.data();
    const uint32_t *pil = lidxs.data(), *pis = sidxs.data(),
                   *pir = ridxs.data();
    const ssize_t lsi = lqs.strides()[0] / sizeof(uint32_t),
                  lsj = lqs.strides()[1] / sizeof(uint32_t);
    const ssize_t ssi = sqs.strides()[0] / sizeof(uint32_t),
                  ssj = sqs.strides()[1] / sizeof(uint32_t);
    const ssize_t rsi = rqs.strides()[0] / sizeof(uint32_t),
                  rsj = rqs.strides()[1] / sizeof(uint32_t);
    assert(memcmp(lqs.strides(), lshs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(memcmp(sqs.strides(), sshs.strides(), 2 * sizeof(ssize_t)) == 0);
    assert(memcmp(rqs.strides(), rshs.strides(), 2 * sizeof(ssize_t)) == 0);
    for (int i = 0; i < n_blocks_s; i++)
        for (int j = pis[i]; j < pis[i + 1]; j++)
            ss.push_back(std::make_tuple(i, j, ps[j]));
    sort(
        ss.begin(), ss.end(),
        [](const tuple<int, int, double> &a, const tuple<int, int, double> &b) {
            return get<2>(a) > get<2>(b);
        });
    vector<tuple<int, int, double>> ss_trunc = ss;
    if (max_dw != 0) {
        int p = 0;
        for (int i = (int)ss_trunc.size(); i > 0; i--) {
            double dw = eigen_values
                            ? get<2>(ss_trunc[i])
                            : get<2>(ss_trunc[i]) * get<2>(ss_trunc[i]);
            if (dw <= max_dw)
                p++;
            else
                break;
        }
        ss_trunc.resize(ss_trunc.size() - p);
    }
    if (cutoff != 0) {
        if (!eigen_values)
            cutoff = sqrt(cutoff);
        for (int i = 1; i < (int)ss_trunc.size(); i++)
            if (get<2>(ss_trunc[i]) < cutoff) {
                ss_trunc.resize(i);
                break;
            }
    }
    if (max_bond_dim != -1 && (int)ss_trunc.size() > max_bond_dim)
        ss_trunc.resize(max_bond_dim);
    sort(
        ss_trunc.begin(), ss_trunc.end(),
        [](const tuple<int, int, double> &a, const tuple<int, int, double> &b) {
            return get<1>(a) < get<1>(b);
        });

    vector<pair<int, vector<int>>> selected;
    selected.reserve(n_blocks_s);
    for (auto &x : ss_trunc)
        if (selected.size() != 0 && selected.back().first == get<0>(x))
            selected.back().second.push_back(get<1>(x));
        else
            selected.push_back(make_pair(get<0>(x), vector<int>{get<1>(x)}));

    int n_blocks_l_new = 0, n_blocks_r_new = 0,
        n_blocks_s_new = (int)selected.size();
    vector<int> nbl(n_blocks_s, 0), nbr(n_blocks_s, 0), skbl(n_blocks_s),
        skbr(n_blocks_s);
    int ikl = 0, ikr = 0, iks = 0;
    int size_l_new = 0, size_r_new = 0, size_s_new = 0;
    vector<int> mask(pis[n_blocks_s], 0);
    for (int i = 0; i < n_blocks_s; i++) {
        uint32_t sql = psqs[i * ssi + 0 * ssj];
        uint32_t sqr = psqs[i * ssi + (ndims - 1) * ssj];
        if (ikl < n_blocks_l && plqs[ikl * lsi + (ndiml - 1) * lsj] == sql)
            skbl[i] = ikl;
        for (; ikl < n_blocks_l && plqs[ikl * lsi + (ndiml - 1) * lsj] == sql;
             ikl++)
            nbl[i]++;
        if (ikr < n_blocks_r && prqs[ikr * rsi + 0 * rsj] == sqr)
            skbr[i] = ikr;
        for (; ikr < n_blocks_r && prqs[ikr * rsi + 0 * rsj] == sqr; ikr++)
            nbr[i]++;
    }
    assert(ikl == n_blocks_l && ikr == n_blocks_r);
    for (auto &m : selected) {
        int iis = m.first, ist = pis[iis], ied = pis[iis + 1];
        uint32_t sql = psqs[iis * ssi + 0 * ssj];
        uint32_t sqr = psqs[iis * ssi + (ndims - 1) * ssj];
        uint32_t ssz = (int)m.second.size();
        for (auto &mm : m.second)
            mask[mm] = 1;
        if (nbl[iis] != 0) {
            uint32_t lsz = (pil[skbl[iis] + nbl[iis]] - pil[skbl[iis]]) /
                           psshs[iis * ssi + 0 * ssj] * ssz;
            n_blocks_l_new += nbl[iis];
            size_l_new += lsz;
        }
        if (nbr[iis] != 0) {
            uint32_t rsz = (pir[skbr[iis] + nbr[iis]] - pir[skbr[iis]]) /
                           psshs[iis * ssi + (ndims - 1) * ssj] * ssz;
            n_blocks_r_new += nbr[iis];
            size_r_new += rsz;
        }
        size_s_new += ssz;
        iks++;
    }
    assert(iks == n_blocks_s_new);
    double error = 0;
    for (int j = 0; j < (int)pis[n_blocks_s]; j++)
        if (!mask[j])
            error += eigen_values ? ps[j] : ps[j] * ps[j];

    py::array_t<uint32_t> nlqs(vector<ssize_t>{n_blocks_l_new, ndiml}),
        nlshs(vector<ssize_t>{n_blocks_l_new, ndiml});
    py::array_t<uint32_t> nlidxs(vector<ssize_t>{n_blocks_l_new + 1});
    py::array_t<uint32_t> nrqs(vector<ssize_t>{n_blocks_r_new, ndimr}),
        nrshs(vector<ssize_t>{n_blocks_r_new, ndimr});
    py::array_t<uint32_t> nridxs(vector<ssize_t>{n_blocks_r_new + 1});
    py::array_t<uint32_t> nsqs(vector<ssize_t>{n_blocks_s_new, ndims}),
        nsshs(vector<ssize_t>{n_blocks_s_new, ndims});
    py::array_t<uint32_t> nsidxs(vector<ssize_t>{n_blocks_s_new + 1});
    py::array_t<double> nldata(vector<ssize_t>{size_l_new});
    py::array_t<double> nrdata(vector<ssize_t>{size_r_new});
    py::array_t<double> nsdata(vector<ssize_t>{size_s_new});

    uint32_t *pnlqs = nlqs.mutable_data(), *pnlshs = nlshs.mutable_data(),
             *pnlidxs = nlidxs.mutable_data();
    uint32_t *pnrqs = nrqs.mutable_data(), *pnrshs = nrshs.mutable_data(),
             *pnridxs = nridxs.mutable_data();
    uint32_t *pnsqs = nsqs.mutable_data(), *pnsshs = nsshs.mutable_data(),
             *pnsidxs = nsidxs.mutable_data();
    double *pnl = nldata.mutable_data(), *pnr = nrdata.mutable_data(),
           *pns = nsdata.mutable_data();
    pnlidxs[0] = pnridxs[0] = pnsidxs[0] = 0;
    ikl = 0, ikr = 0, iks = 0;
    int iknl = 0, iknr = 0;
    for (auto &m : selected) {
        int iis = m.first, ist = pis[iis], ied = pis[iis + 1];
        int ssz = (int)m.second.size(), fsz = psshs[iis * ssi + 0 * ssj];
        pnsqs[iks] = psqs[iis];
        pnsshs[iks] = ssz;
        pnsidxs[iks + 1] = pnsidxs[iks] + ssz;
        for (uint32_t i = 0; i < ssz; i++)
            pns[pnsidxs[iks] + i] = ps[m.second[i]];
        for (int i = 0; i < nbl[iis]; i++) {
            ikl = skbl[iis];
            for (int j = 0; j < ndiml; j++) {
                pnlqs[(iknl + i) * ndiml + j] = plqs[(ikl + i) * lsi + j * lsj];
                pnlshs[(iknl + i) * ndiml + j] =
                    plshs[(ikl + i) * lsi + j * lsj];
            }
            pnlshs[(iknl + i) * ndiml + ndiml - 1] = ssz;
            int lszl = (pil[ikl + i + 1] - pil[ikl + i]) / fsz;
            uint32_t lsz = lszl * ssz;
            pnlidxs[iknl + i + 1] = pnlidxs[iknl + i] + lsz;
            for (uint32_t j = 0; j < ssz; j++)
                dcopy(&lszl, pl + pil[ikl + i] + m.second[j] - ist, &fsz,
                      pnl + pnlidxs[iknl + i] + j, &ssz);
        }
        for (int i = 0; i < nbr[iis]; i++) {
            ikr = skbr[iis];
            for (int j = 0; j < ndimr; j++) {
                pnrqs[(iknr + i) * ndimr + j] = prqs[(ikr + i) * rsi + j * rsj];
                pnrshs[(iknr + i) * ndimr + j] =
                    prshs[(ikr + i) * rsi + j * rsj];
            }
            pnrshs[(iknr + i) * ndimr + 0] = ssz;
            int rszr = (pir[ikr + i + 1] - pir[ikr + i]) / fsz, inc = 1;
            uint32_t rsz = rszr * ssz;
            pnridxs[iknr + i + 1] = pnridxs[iknr + i] + rsz;
            for (uint32_t j = 0; j < ssz; j++)
                dcopy(&rszr, pr + pir[ikr + i] + (m.second[j] - ist) * rszr,
                      &inc, pnr + pnridxs[iknr + i] + j * rszr, &inc);
        }
        iknl += nbl[iis], iknr += nbr[iis];
        iks++;
    }

    return std::make_tuple(nlqs, nlshs, nldata, nlidxs, nsqs, nsshs, nsdata,
                           nsidxs, nrqs, nrshs, nrdata, nridxs, error);
}
