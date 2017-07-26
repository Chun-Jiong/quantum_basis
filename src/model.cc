#include <iostream>
//#include <fstream>
#include <iomanip>
#include <climits>
#include <random>
#include "qbasis.h"
#include "graph.h"

namespace qbasis {
    template <typename T>
    model<T>::model(): matrix_free(true), nconv(0),
                       dim_target_full(0), dim_excite_full(0),
                       dim_target_repr(0), dim_excite_repr(0)
    {}
    
    template <typename T>
    uint32_t model<T>::local_dimension() const
    {
        uint32_t res = 1;
        for (decltype(props.size()) j = 0; j < props.size(); j++) res *= props[j].dim_local;
        return res;
    }
    
    template <typename T>
    void model<T>::check_translation(const lattice &latt)
    {
        std::cout << "Checking translational symmetry." << std::endl;
        std::cout << "In the future replace this check with serious stuff!" << std::endl;
        trans_sym.clear();
        auto bc = latt.boundary();
        for (uint32_t j = 0; j < latt.dimension(); j++) {
            if (bc[j] == "pbc" || bc[j] == "PBC") {
                trans_sym.push_back(true);
            } else {
                trans_sym.push_back(false);
            }
        }
        std::cout << std::endl;
    }
    
    template <typename T>
    void model<T>::fill_Weisse_table(const lattice &latt)
    {
        // first check translation symmetry
        check_translation(latt);
        
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        auto props_sub = props_sub_a;
        auto latt_sub = divide_lattice(latt);
        std::cout << "Generating sublattice full basis... " << std::endl;
        std::vector<mbasis_elem> basis_sub_full;
        enumerate_basis<T>(props_sub, basis_sub_full);
        sort_basis_normal_order(basis_sub_full);                                // has to be sorted in the normal way
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "Elapsed time for generating sublattice full basis: " << elapsed_seconds.count() << "s." << std::endl;
        start = end;
        
        std::cout << "Classifying sublattice basis... " << std::flush;
        classify_trans_full2rep(props_sub, basis_sub_full, latt_sub, trans_sym, basis_sub_repr, belong2rep_sub, dist2rep_sub);
        classify_trans_rep2group(props_sub, basis_sub_repr, latt_sub, trans_sym, groups_sub, omega_g_sub, belong2group_sub);
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;
        std::cout << elapsed_seconds.count() << "s." << std::endl;
        start = end;
        
        // double checking correctness
        uint64_t check_dim_sub_full = 0;
        for (decltype(basis_sub_repr.size()) j = 0; j < basis_sub_repr.size(); j++) check_dim_sub_full += omega_g_sub[belong2group_sub[j]];
        assert(check_dim_sub_full == static_cast<uint64_t>(basis_sub_full.size()));
        
        std::cout << "Generating maps (ga,gb,ja,jb) -> (i,j) and (ga,gb,j) -> w ... " << std::flush;
        classify_Weisse_tables(props, props_sub, basis_sub_full, basis_sub_repr, latt, trans_sym,
                               belong2rep_sub, dist2rep_sub, groups_sub, omega_g_sub, belong2group_sub,
                               Weisse_e_lt, Weisse_e_eq, Weisse_e_gt, Weisse_w_lt, Weisse_w_eq);
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;
        std::cout << elapsed_seconds.count() << "s." << std::endl;
        std::cout << std::endl;
    }
    
    
    // need further optimization! (for example, special treatment of dilute limit; special treatment of quantum numbers; quick sort of sign)
    template <typename T>
    void model<T>::enumerate_basis_full(MKL_INT &dim_full,
                                        std::vector<qbasis::mbasis_elem> &basis_full,
                                        std::vector<MKL_INT> &Lin_Ja,
                                        std::vector<MKL_INT> &Lin_Jb,
                                        std::vector<mopr<T>> conserve_lst,
                                        std::vector<double> val_lst)
    {
        // checking if reaching code capability
        MKL_INT mkl_int_max = LLONG_MAX;
        if (mkl_int_max != LLONG_MAX) {
            mkl_int_max = INT_MAX;
            //std::cout << "int_max = " << INT_MAX << std::endl;
            assert(mkl_int_max == INT_MAX);
            std::cout << "Using 32-bit integers." << std::endl;
        } else {
            std::cout << "Using 64-bit integers." << std::endl;
        }
        assert(mkl_int_max > 0);
        
        enumerate_basis<T>(props, basis_full, conserve_lst, val_lst);
        
        dim_full = static_cast<MKL_INT>(basis_full.size());
        
        sort_basis_Lin_order(props, basis_full);
        
        fill_Lin_table(props, basis_full, Lin_Ja, Lin_Jb);
    }
    
    
    template <typename T>
    void model<T>::enumerate_basis_repr(const lattice &latt,
                                        const std::vector<int> &momentum,
                                        MKL_INT &dim_repr,
                                        std::vector<qbasis::mbasis_elem> &basis_repr,
                                        std::vector<MKL_INT> &Lin_Ja,
                                        std::vector<MKL_INT> &Lin_Jb,
                                        MltArray_double &Weisse_nu_lt,
                                        MltArray_double &Weisse_nu_eq,
                                        std::vector<mopr<T>> conserve_lst,
                                        std::vector<double> val_lst)
    {
        assert(latt.dimension() == static_cast<uint32_t>(momentum.size()));
        assert(conserve_lst.size() == val_lst.size());
        assert(basis_sub_repr.size() > 0);   // should be already generated when filling Weisse Tables
        
        // checking if reaching code capability
        MKL_INT mkl_int_max = LLONG_MAX;
        if (mkl_int_max != LLONG_MAX) {
            mkl_int_max = INT_MAX;
            //std::cout << "int_max = " << INT_MAX << std::endl;
            assert(mkl_int_max == INT_MAX);
            std::cout << "Using 32-bit integers." << std::endl;
        } else {
            std::cout << "Using 64-bit integers." << std::endl;
        }
        assert(mkl_int_max > 0);
        
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        auto L = latt.Linear_size();
        auto momentum2 = momentum;
        std::cout << "Enumerating basis_repr according to momentum: (" << std::flush;
        for (uint32_t j = 0; j < momentum.size(); j++) {
            if (trans_sym[j]) {
                std::cout << momentum[j] << "\t";
            } else {
                std::cout << "NA\t";
            }
            while (momentum2[j] < 0) momentum2[j] += static_cast<int>(L[j]);
        }
        std::cout << ")..." << std::endl;
        
        // first calculate the normalization factors
        auto base = Weisse_w_lt.linear_size();
        Weisse_nu_lt = MltArray_double(base, 0.0);
        Weisse_nu_eq = MltArray_double(base, 0.0);
        std::vector<uint64_t> pos(base.size(),0);
        while (! dynamic_base_overflow(pos, base)) {
            auto omega_lt = Weisse_w_lt.index(pos);
            assert(omega_lt.size() == momentum.size());
            if (std::any_of(omega_lt.begin(), omega_lt.end(), [](uint32_t i){ return i != 0; })) {
                Weisse_nu_lt.index(pos) = 1.0;
                for (decltype(momentum.size()) j = 0; j < momentum.size(); j++) {
                    assert(L[j] % omega_lt[j] == 0);
                    Weisse_nu_lt.index(pos) *= (momentum2[j] % static_cast<int>(L[j] / omega_lt[j]) == 0
                                                ? static_cast<double>(omega_lt[j]) : 0.0);
                }
            }
            auto omega_eq = Weisse_w_eq.index(pos);
            assert(omega_eq.size() == momentum.size());
            if (std::any_of(omega_eq.begin(), omega_eq.end(), [](uint32_t i){ return i != 0; })) {
                Weisse_nu_eq.index(pos) = 1.0;
                for (decltype(momentum.size()) j = 0; j < momentum.size(); j++) {
                    assert(L[j] % omega_eq[j] == 0);
                    Weisse_nu_eq.index(pos) *= (momentum2[j] % static_cast<int>(L[j] / omega_eq[j]) == 0
                                                ? static_cast<double>(omega_eq[j]) : 0.0);
                }
            }
            pos = dynamic_base_plus1(pos, base);
        }
        
        // now really enumerate representatives
        auto latt_sub = divide_lattice(latt);
        basis_repr.clear();
        for (decltype(basis_sub_repr.size()) ra = 0; ra < basis_sub_repr.size(); ra++) {
            auto ga = belong2group_sub[ra];
            int sgn;
            for (decltype(ra) rb = ra; rb < basis_sub_repr.size(); rb++) {
                auto gb = belong2group_sub[rb];
                std::vector<uint32_t> disp_j(latt_sub.dimension(),0);
                std::vector<int> disp_j_int(disp_j.size());
                while (! dynamic_base_overflow(disp_j, groups_sub[gb])) {
                    auto pos = std::vector<uint64_t>{ga,gb};
                    pos.insert(pos.end(), disp_j.begin(), disp_j.end());
                    auto omega = (ra < rb)?(Weisse_w_lt.index(pos)):(Weisse_w_eq.index(pos));
                    double nu  = (ra < rb)?(Weisse_nu_lt.index(pos)):(Weisse_nu_eq.index(pos));
                    if (std::any_of(omega.begin(), omega.end(), [](uint32_t i){ return i != 0; }) &&
                        std::abs(nu) > machine_prec) {  // valid representative
                        mbasis_elem rb_new = basis_sub_repr[rb];
                        for (uint32_t j = 0; j < latt_sub.dimension(); j++) disp_j_int[j] = static_cast<int>(disp_j[j]);
                        rb_new.translate(props_sub_b, latt_sub, disp_j_int, sgn);
                        mbasis_elem ra_z_Tj_rb;
                        zipper_basis(props, props_sub_a, props_sub_b, basis_sub_repr[ra], rb_new, ra_z_Tj_rb);
                        
                        // check if the symmetries are obeyed
                        bool flag = true;
                        auto it_opr = conserve_lst.begin();
                        auto it_val = val_lst.begin();
                        while (it_opr != conserve_lst.end()) {
                            auto temp = ra_z_Tj_rb.diagonal_operator(props, *it_opr);
                            if (std::abs(temp - *it_val) >= 1e-5) {
                                flag = false;
                                break;
                            }
                            it_opr++;
                            it_val++;
                        }
                        if (flag) basis_repr.push_back(ra_z_Tj_rb);
                    }
                    disp_j = dynamic_base_plus1(disp_j, groups_sub[gb]);
                }
            }
        }
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout <<  elapsed_seconds.count() << "s." << std::endl;
        dim_repr = static_cast<MKL_INT>(basis_repr.size());
        std::cout << "dim_repr (without removing dulplicates) = " << dim_repr << std::endl;
        
        sort_basis_Lin_order(props, basis_repr);
        
        fill_Lin_table(props, basis_repr, Lin_Ja, Lin_Jb);
    }
    
    
    
    
    
    template <typename T>
    void model<T>::basis_init_repr_deprecated(const std::vector<int> &momentum, const lattice &latt)
    {
        assert(latt.dimension() == static_cast<uint32_t>(momentum.size()));
        assert(dim_target_full > 0 && dim_target_full == basis_target_full.size());
        assert(Lin_Ja_target_full.size() > 0 && Lin_Jb_target_full.size() > 0);
        check_translation(latt);
        
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        std::cout << "Classifying basis_repr according to momentum: (";
        for (uint32_t j = 0; j < momentum.size(); j++) {
            if (trans_sym[j]) {
                std::cout << momentum[j] << "\t";
            } else {
                std::cout << "NA\t";
            }
        }
        std::cout << ")..." << std::endl;
        
        std::cout << "-------- if all dims obc, let's stop here (to be implemented) -------" << std::endl;
        
        auto num_sub = latt.num_sublattice();
        auto L = latt.Linear_size();
        basis_belong_deprec.resize(dim_target_full);
        std::fill(basis_belong_deprec.begin(), basis_belong_deprec.end(), -1);
        basis_coeff_deprec.resize(dim_target_full);
        std::fill(basis_coeff_deprec.begin(), basis_coeff_deprec.end(), std::complex<double>(0.0,0.0));
        for (MKL_INT i = 0; i < dim_target_full; i++) {
            if (basis_belong_deprec[i] != -1) continue;
            basis_belong_deprec[i] = i;
            basis_coeff_deprec[i] = std::complex<double>(1.0, 0.0);
            #pragma omp parallel for schedule(dynamic,1)
            for (uint32_t site = num_sub; site < latt.total_sites(); site += num_sub) {
                std::vector<int> disp;
                int sub, sgn;
                latt.site2coor(disp, sub, site);
                bool flag = false;
                for (uint32_t d = 0; d < latt.dimension(); d++) {
                    if (!trans_sym[d] && disp[d] != 0) {
                        flag = true;
                        break;
                    }
                }
                if (flag) continue;            // such translation forbidden
                auto basis_temp = basis_target_full[i];
                basis_temp.translate(props, latt, disp, sgn);
                uint64_t i_a, i_b;
                basis_temp.label_sub(props, i_a, i_b);
                MKL_INT j = Lin_Ja_target_full[i_a] + Lin_Jb_target_full[i_b];
                double exp_coef = 0.0;
                for (uint32_t d = 0; d < latt.dimension(); d++) {
                    if (trans_sym[d]) {
                        exp_coef += momentum[d] * disp[d] / static_cast<double>(L[d]);
                    }
                }
                auto coef = std::exp(std::complex<double>(0.0, 2.0 * pi * exp_coef));
                if (sgn % 2 == 1) coef *= std::complex<double>(-1.0, 0.0);
                #pragma omp critical
                {
                    basis_belong_deprec[j] = i;
                    basis_coeff_deprec[j] += coef;
                }
            }
        }
        
        std::list<MKL_INT> temp_repr;
        temp_repr.push_back(0);
        dim_target_repr = 1;
        for (MKL_INT j = 1; j < dim_target_full; j++) {
            if (basis_belong_deprec[j] > temp_repr.back()) {
                dim_target_repr++;
                temp_repr.push_back(basis_belong_deprec[j]);
            }
        }
        MKL_INT redundant = 0;
        auto it = temp_repr.begin();
        while (it != temp_repr.end()) {
            if (std::abs(basis_coeff_deprec[*it]) < opr_precision) {
                it = temp_repr.erase(it);
                redundant++;
                dim_target_repr--;
            } else {
                //std::cout << std::abs(std::imag(basis_coeff[*it])) << std::endl;
                assert(std::abs(std::imag(basis_coeff_deprec[*it])) < opr_precision);
                it++;
            }
        }
        assert(dim_target_repr == static_cast<MKL_INT>(temp_repr.size()));
        if (redundant > 0) std::cout << redundant << " redundant reprs removed." << std::endl;
        basis_repr_deprec.resize(dim_target_repr);
        std::copy(temp_repr.begin(), temp_repr.end(), basis_repr_deprec.begin());
        assert(is_sorted_norepeat(basis_repr_deprec));
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
    }
    
    

    
    
    

    
    
    template <typename T>
    void model<T>::generate_Ham_sparse_full(const bool &upper_triangle)
    {
        if (matrix_free) matrix_free = false;     //
        assert(Lin_Ja_target_full.size() > 0 && Lin_Jb_target_full.size() > 0);
        
        std::cout << "Generating LIL Hamiltonian matrix..." << std::endl;
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        lil_mat<T> matrix_lil(dim_target_full, upper_triangle);
        #pragma omp parallel for schedule(dynamic,1)
        for (MKL_INT i = 0; i < dim_target_full; i++) {
            for (uint32_t cnt = 0; cnt < Ham_diag.size(); cnt++)                                       // diagonal part:
                matrix_lil.add(i, i, basis_target_full[i].diagonal_operator(props, Ham_diag[cnt]));
            qbasis::wavefunction<T> intermediate_state = oprXphi(Ham_off_diag, basis_target_full[i], props);  // non-diagonal part:
            for (decltype(intermediate_state.size()) cnt = 0; cnt < intermediate_state.size(); cnt++) {
                auto &ele_new = intermediate_state[cnt];
                uint64_t i_a, i_b;
                ele_new.first.label_sub(props, i_a, i_b);
                MKL_INT j = Lin_Ja_target_full[i_a] + Lin_Jb_target_full[i_b];                  // < j | H | i > obtained
                assert(j >= 0 && j < dim_target_full);
                if (upper_triangle) {
                    if (i <= j) matrix_lil.add(i, j, conjugate(ele_new.second));
                } else {
                    matrix_lil.add(i, j, conjugate(ele_new.second));
                }
            }
        }
        HamMat_csr_target_full = csr_mat<T>(matrix_lil);
        std::cout << "Hamiltonian CSR matrix generated." << std::endl;
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
    }
    
    
    template <typename T>
    void model<T>::generate_Ham_sparse_repr(const bool &upper_triangle)
    {
        if (dim_target_repr < 1) {
            std::cout << "dim_repr = " << dim_target_repr << "!!!" << std::endl;
            return;
        }
        std::cout << "Generating Hamiltonian Matrix..." << std::endl;
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        lil_mat<std::complex<double>> matrix_lil(dim_target_repr, upper_triangle);
        #pragma omp parallel for schedule(dynamic,1)
        for (MKL_INT i = 0; i < dim_target_repr; i++) {
            auto repr_i = basis_repr_deprec[i];
            for (uint32_t cnt = 0; cnt < Ham_diag.size(); cnt++)                                            // diagonal part:
                matrix_lil.add(i, i, basis_target_full[repr_i].diagonal_operator(props,Ham_diag[cnt]));
            qbasis::wavefunction<T> intermediate_state = oprXphi(Ham_off_diag, basis_target_full[repr_i], props);  // non-diagonal part:
            for (uint32_t cnt = 0; cnt < intermediate_state.size(); cnt++) {
                auto &ele_new = intermediate_state[cnt];
                uint64_t i_a, i_b;
                ele_new.first.label_sub(props, i_a, i_b);
                MKL_INT state_j = Lin_Ja_target_full[i_a] + Lin_Jb_target_full[i_b];
                assert(state_j >= 0 && state_j < dim_target_full);
                auto repr_j = basis_belong_deprec[state_j];
                auto j = binary_search<MKL_INT,MKL_INT>(basis_repr_deprec, repr_j, 0, dim_target_repr);                 // < j |P'_k H | i > obtained
                if (j < 0 || j >= dim_target_repr) continue;
                auto coeff = basis_coeff_deprec[state_j]/std::sqrt(std::real(basis_coeff_deprec[repr_i] * basis_coeff_deprec[repr_j]));
                if (upper_triangle) {
                    if (i <= j) matrix_lil.add(i, j, conjugate(ele_new.second) * coeff);
                } else {
                    matrix_lil.add(i, j, conjugate(ele_new.second) * coeff);
                }
            }
        }
        HamMat_csr_target_repr = csr_mat<std::complex<double>>(matrix_lil);
        std::cout << "Hamiltonian generated." << std::endl;
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
    }
    
    template <typename T>
    std::vector<std::complex<double>> model<T>::to_dense()
    {
        std::cout << "Fall back to use matrix explicitly." << std::endl;
        generate_Ham_sparse_full();
        return HamMat_csr_target_full.to_dense();
    }
    
    template <typename T>
    void model<T>::MultMv(const T *x, T *y) const
    {
        assert(matrix_free);
        std::cout << "*" << std::flush;
        assert(Lin_Ja_target_full.size() > 0 && Lin_Jb_target_full.size() > 0);
        
        #pragma omp parallel for schedule(dynamic,1)
        for (MKL_INT i = 0; i < dim_target_full; i++) {
            y[i] = static_cast<T>(0.0);
            if (std::abs(x[i]) > machine_prec) {
                for (uint32_t cnt = 0; cnt < Ham_diag.size(); cnt++)
                    y[i] += x[i] * basis_target_full[i].diagonal_operator(props, Ham_diag[cnt]);
            }
            qbasis::wavefunction<T> intermediate_state = oprXphi(Ham_off_diag, basis_target_full[i], props);
            for (decltype(intermediate_state.size()) cnt = 0; cnt < intermediate_state.size(); cnt++) {
                auto &ele_new = intermediate_state[cnt];
                if (std::abs(ele_new.second) > opr_precision) {
                    uint64_t i_a, i_b;
                    ele_new.first.label_sub(props, i_a, i_b);
                    MKL_INT j = Lin_Ja_target_full[i_a] + Lin_Jb_target_full[i_b];                // < j | H | i > obtained
                    assert(j >= 0 && j < dim_target_full);
                    if (std::abs(x[j]) > machine_prec) y[i] += (x[j] * conjugate(ele_new.second));
                }
            }
        }
    }
    
    template <typename T>
    void model<T>::MultMv(T *x, T *y)
    {
        assert(matrix_free);
        std::cout << "*" << std::flush;
        assert(Lin_Ja_target_full.size() > 0 && Lin_Jb_target_full.size() > 0);
        
        #pragma omp parallel for schedule(dynamic,1)
        for (MKL_INT i = 0; i < dim_target_full; i++) {
            y[i] = static_cast<T>(0.0);
            if (std::abs(x[i]) > machine_prec) {
                for (uint32_t cnt = 0; cnt < Ham_diag.size(); cnt++)
                    y[i] += x[i] * basis_target_full[i].diagonal_operator(props, Ham_diag[cnt]);
            }
            qbasis::wavefunction<T> intermediate_state = oprXphi(Ham_off_diag, basis_target_full[i], props);
            for (decltype(intermediate_state.size()) cnt = 0; cnt < intermediate_state.size(); cnt++) {
                auto &ele_new = intermediate_state[cnt];
                if (std::abs(ele_new.second) > opr_precision) {
                    uint64_t i_a, i_b;
                    ele_new.first.label_sub(props, i_a, i_b);
                    MKL_INT j = Lin_Ja_target_full[i_a] + Lin_Jb_target_full[i_b];                // < j | H | i > obtained
                    assert(j >= 0 && j < dim_target_full);
                    if (std::abs(x[j]) > machine_prec) y[i] += (x[j] * conjugate(ele_new.second));
                }
            }
        }
    }
    
    
    template <typename T>
    void model<T>::locate_E0_full(const MKL_INT &nev, const MKL_INT &ncv, MKL_INT maxit)
    {
        assert(nev > 0);
        assert(ncv > nev + 1);
        if (maxit <= 0) maxit = nev * 100; // arpack default
        
        std::cout << "Calculating ground state..." << std::endl;
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            if (tid == 0) {
                std::cout << "Number of procs   = " << omp_get_num_procs() << std::endl;
                std::cout << "Number of OMP threads = " << omp_get_num_threads() << std::endl;
            }
        }
        std::cout << "Number of MKL threads = " << mkl_get_max_threads() << std::endl << std::endl;
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        std::vector<T> v0(dim_target_full, 1.0);
        eigenvals_full.resize(nev);
        eigenvecs_full.resize(dim_target_full * nev);
        if (matrix_free) {
            iram(dim_target_full, *this, v0.data(), nev, ncv, maxit, "sr", nconv, eigenvals_full.data(), eigenvecs_full.data());
        } else {
            iram(dim_target_full, HamMat_csr_target_full, v0.data(), nev, ncv, maxit, "sr", nconv, eigenvals_full.data(), eigenvecs_full.data());
        }
        assert(nconv > 0);
        E0 = eigenvals_full[0];
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
        std::cout << "E0   = " << E0 << std::endl;
        if (nconv > 1) {
            gap = eigenvals_full[1] - eigenvals_full[0];
            std::cout << "Gap  = " << gap << std::endl;
        }
    }
    
    template <typename T>
    void model<T>::locate_E0_full_lanczos()
    {
        assert(false);
        std::cout << "Calculating ground state (with simple Lanczos)..." << std::endl;
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            if (tid == 0) {
                std::cout << "Number of procs   = " << omp_get_num_procs() << std::endl;
                std::cout << "Number of OMP threads = " << omp_get_num_threads() << std::endl;
            }
        }
        std::cout << "Number of MKL threads = " << mkl_get_max_threads() << std::endl << std::endl;
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        
        std::default_random_engine generator;
        std::uniform_real_distribution<double> distribution(-1.0,1.0);
        std::vector<T> resid(dim_target_full), v(dim_target_full*3);
        for (MKL_INT j = 0; j < dim_target_full; j++) resid[j] = static_cast<T>(distribution(generator));
        double rnorm = nrm2(dim_target_full, resid.data(), 1);
        scal(dim_target_full, 1.0 / rnorm, resid.data(), 1);
        MKL_INT ldh = 2000;                                     // at most 2000 steps
        std::vector<double> hessenberg(ldh, 0.0), ritz(ldh), e(ldh-1);
        
        MKL_INT total_steps = 0;
        MKL_INT step = 6;
        E0 = 1.0e10;
        std::vector<std::pair<double, MKL_INT>> eigenvals(ldh);
        eigenvals[0].first = 1.0e9;
        while (std::abs(E0 - eigenvals[0].first) > lanczos_precision && total_steps < ldh) {
            if (eigenvals[0].first < E0) {
                E0 = eigenvals[0].first;
            }
            if (matrix_free) {
                lanczos(0, step, dim_target_full, *this, rnorm, resid.data(), v.data(), hessenberg.data(), 2000, false);
            } else {
                lanczos(0, step, dim_target_full, HamMat_csr_target_full, rnorm, resid.data(), v.data(), hessenberg.data(), 2000, false);
            }
            total_steps += step;
            copy(total_steps, hessenberg.data() + ldh, 1, ritz.data(), 1);
            copy(total_steps-1, hessenberg.data() + 1, 1, e.data(), 1);
            int info = sterf(total_steps, ritz.data(), e.data());
            assert(info == 0);
            for (decltype(eigenvals.size()) j = 0; j < eigenvals.size(); j++) {
                eigenvals[j].first = ritz[j];
                eigenvals[j].second = static_cast<MKL_INT>(j);
            }
            std::sort(eigenvals.begin(), eigenvals.end(),
                      [](const std::pair<double, MKL_INT> &a, const std::pair<double, MKL_INT> &b){ return a.first < b.first; });
            std::cout << "Lanczos steps: " << total_steps << std::endl;
            std::cout << "Ritz values: "
                      << std::setw(25) << eigenvals[0].first << std::setw(25) << eigenvals[1].first
                      << std::setw(25) << eigenvals[2].first << std::setw(25) << eigenvals[3].first
                      << std::setw(25) << eigenvals[4].first << std::endl;
        }
        assert(total_steps < ldh);
        
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
        std::cout << "E0   = " << E0 << std::endl;
    }
    
    template <typename T>
    void model<T>::locate_Emax_full(const MKL_INT &nev, const MKL_INT &ncv, MKL_INT maxit)
    {
        assert(ncv > nev + 1);
        if (maxit <= 0) maxit = nev * 100; // arpack default
        std::cout << "Calculating highest energy state..." << std::endl;
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            if (tid == 0) {
                std::cout << "Number of procs   = " << omp_get_num_procs() << std::endl;
                std::cout << "Number of OMP threads = " << omp_get_num_threads() << std::endl;
            }
        }
        std::cout << "Number of MKL threads = " << mkl_get_max_threads() << std::endl << std::endl;
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        std::vector<T> v0(HamMat_csr_target_full.dimension(), 1.0);
        eigenvals_full.resize(nev);
        eigenvecs_full.resize(HamMat_csr_target_full.dimension() * nev);
        if (matrix_free) {
            iram(dim_target_full, *this, v0.data(), nev, ncv, maxit, "lr", nconv, eigenvals_full.data(), eigenvecs_full.data());
        } else {
            iram(dim_target_full, HamMat_csr_target_full, v0.data(), nev, ncv, maxit, "lr", nconv, eigenvals_full.data(), eigenvecs_full.data());
        }
        assert(nconv > 0);
        Emax = eigenvals_full[0];
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
        std::cout << "Emax = " << Emax << std::endl;
    }
    
    template <typename T>
    void model<T>::locate_E0_repr(const MKL_INT &nev, const MKL_INT &ncv, MKL_INT maxit)
    {
        assert(ncv > nev + 1);
        if (maxit <= 0) maxit = nev * 100; // arpack default
        std::cout << "Calculating ground state in the subspace..." << std::endl;
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            if (tid == 0) {
                std::cout << "Number of procs   = " << omp_get_num_procs() << std::endl;
                std::cout << "Number of OMP threads = " << omp_get_num_threads() << std::endl;
            }
        }
        std::cout << "Number of MKL threads = " << mkl_get_max_threads() << std::endl << std::endl;
        if (dim_target_repr < 1) {
            std::cout << "dim_repr = " << dim_target_repr << "!!!" << std::endl;
            return;
        }
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        std::vector<std::complex<double>> v0(HamMat_csr_target_repr.dimension(), 1.0);
        eigenvals_repr.resize(nev);
        eigenvecs_repr.resize(HamMat_csr_target_repr.dimension() * nev);
        iram(dim_target_repr, HamMat_csr_target_repr, v0.data(), nev, ncv, maxit, "sr", nconv, eigenvals_repr.data(), eigenvecs_repr.data());
        assert(nconv > 1);
        E0 = eigenvals_repr[0];
        gap = eigenvals_repr[1] - eigenvals_repr[0];
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
        std::cout << "E0   = " << E0 << std::endl;
        std::cout << "Gap  = " << gap << std::endl;
    }
    
    template <typename T>
    void model<T>::locate_Emax_repr(const MKL_INT &nev, const MKL_INT &ncv, MKL_INT maxit)
    {
        assert(ncv > nev + 1);
        if (maxit <= 0) maxit = nev * 100; // arpack default
        std::cout << "Calculating highest energy state in the subspace..." << std::endl;
        if (dim_target_repr < 1) {
            std::cout << "dim_repr = " << dim_target_repr << "!!!" << std::endl;
            return;
        }
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        std::vector<std::complex<double>> v0(HamMat_csr_target_repr.dimension(), 1.0);
        eigenvals_repr.resize(nev);
        eigenvecs_repr.resize(HamMat_csr_target_repr.dimension() * nev);
        iram(dim_target_repr, HamMat_csr_target_repr, v0.data(), nev, ncv, maxit, "lr", nconv, eigenvals_repr.data(), eigenvecs_repr.data());
        assert(nconv > 0);
        Emax = eigenvals_repr[0];
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
        std::cout << "Emax = " << Emax << std::endl;
    }
    
    template <typename T>
    void model<T>::moprXeigenvec_full(const mopr<T> &lhs, T* vec_new, const MKL_INT &which_col)
    {
        assert(which_col >= 0 && which_col < nconv);
        std::cout << "mopr * eigenvec ..." << std::endl;
        for (MKL_INT j = 0; j < dim_target_full; j++) vec_new[j] = 0.0;
//        // leave these lines for a while, for openmp debugging purpose
//        static MKL_INT debug_flag = 0;
//        std::vector<std::list<MKL_INT>> jobid_list(100);
//        std::vector<std::list<double>> job_start(100), job_start_wait(100), job_finish_wait(100);
        
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();

        MKL_INT base = dim_target_full * which_col;
        
        #pragma omp parallel for schedule(dynamic,16)
        for (MKL_INT j = 0; j < dim_target_full; j++) {
            if (std::abs(eigenvecs_full[base + j]) < lanczos_precision) continue;
//                std::chrono::time_point<std::chrono::system_clock> enter_time, start_wait, finish_wait;
//                enter_time = std::chrono::system_clock::now();
            std::vector<std::pair<MKL_INT, T>> values;
            for (uint32_t cnt_opr = 0; cnt_opr < lhs.size(); cnt_opr++) {
                auto &A = lhs[cnt_opr];
                auto temp = eigenvecs_full[base + j];
                if (A.q_diagonal()) {
                    values.push_back(std::pair<MKL_INT, T>(j,temp * basis_target_full[j].diagonal_operator(props,A)));
                } else {
                    auto intermediate_state = oprXphi(A, basis_target_full[j], props);
                    for (uint32_t cnt = 0; cnt < intermediate_state.size(); cnt++) {
                        auto &ele = intermediate_state[cnt];
                        values.push_back(std::pair<MKL_INT, T>(binary_search<mbasis_elem,MKL_INT>(basis_target_full, ele.first, 0, dim_target_full), temp * ele.second));
                    }
                }
            }
//                // previously hope the sort helps the speed of the critical region,
//                // however the overhead is too much
//                std::sort(values.begin(), values.end(),
//                          [](const std::pair<MKL_INT, T> &a, const std::pair<MKL_INT, T> &b){ return a.first < b.first; });
//                start_wait = std::chrono::system_clock::now();
            #pragma omp critical
            {
                for (decltype(values.size()) cnt = 0; cnt < values.size(); cnt++)
                    vec_new[values[cnt].first] += values[cnt].second;
//                    finish_wait = std::chrono::system_clock::now();
//                    auto id = omp_get_thread_num();
//                    std::chrono::duration<double> elapsed_seconds;
//                    jobid_list[id].push_back(j);
//                    elapsed_seconds = enter_time - start;
//                    job_start[id].push_back(elapsed_seconds.count());
//                    elapsed_seconds = start_wait - start;
//                    job_start_wait[id].push_back(elapsed_seconds.count());
//                    elapsed_seconds = finish_wait - start;
//                    job_finish_wait[id].push_back(elapsed_seconds.count());
            }
        }
            

        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s." << std::endl;
        
//        #pragma omp parallel
//        {
//            int tid = omp_get_thread_num();
//            if (tid == 0) {
//                if (! debug_flag) {
//                    for (MKL_INT j = 0; j < omp_get_num_threads(); j++) {
//                        std::string output_name = "thread_"+std::to_string(j)+".dat";
//                        std::ofstream fout(output_name, std::ios::out | std::ios::app);
//                        auto it0 = jobid_list[j].begin();
//                        auto it1 = job_start[j].begin();
//                        auto it2 = job_start_wait[j].begin();
//                        auto it3 = job_finish_wait[j].begin();
//                        while(it0 != jobid_list[j].end()){
//                            fout << std::setw(30) << *it0 << std::setw(30) << *it1
//                            << std::setw(30) << *it2 << std::setw(30) << *it3 << std::endl;
//                            it0++; it1++; it2++; it3++;
//                        }
//                        fout.close();
//                    }
//                    debug_flag = 1;
//                }
//            }
//        }
        
    }
    
    // need full rewrite below
    template <typename T>
    T model<T>::measure(const mopr<T> &lhs, const MKL_INT &which_col)
    {
        assert(which_col >= 0 && which_col < nconv);
        if (HamMat_csr_target_full.dimension() == dim_target_full) {
            MKL_INT base = dim_target_full * which_col;
            std::vector<T> vec_new(dim_target_full);
            moprXeigenvec_full(lhs, vec_new.data(), which_col);
            return dotc(dim_target_full, eigenvecs_full.data() + base, 1, vec_new.data(), 1);
        } else {
            std::cout << "not implemented yet" << std::endl;
            return static_cast<T>(0.0);
        }
    }
    
    template <typename T>
    T model<T>::measure(const mopr<T> &lhs1, const mopr<T> &lhs2, const MKL_INT &which_col)
    {
        assert(which_col >= 0 && which_col < nconv);
        if (HamMat_csr_target_full.dimension() == dim_target_full) {
            std::vector<T> vec_new1(dim_target_full);
            std::vector<T> vec_new2(dim_target_full);
            moprXeigenvec_full(lhs1, vec_new1.data(), which_col);
            moprXeigenvec_full(lhs2, vec_new2.data(), which_col);
            return dotc(dim_target_full, vec_new1.data(), 1, vec_new2.data(), 1);
        } else {
            std::cout << "not implemented yet" << std::endl;
            return static_cast<T>(0.0);
        }
    }
    
    
    // Explicit instantiation
    //template class model<double>;
    template class model<std::complex<double>>;

}
