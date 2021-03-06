#include "MVMO.h"
#include "MVMO_util.h"
#include <iostream>
#include <set>
using namespace std;
using namespace Eigen;

MVMO::MVMO(MVMO::MVMO_Obj f, const VectorXd& lb, const VectorXd& ub)
    : _dim(lb.size()), _lb(lb), _ub(ub), _f(f)
{
    assert(lb.size() == ub.size());
    assert((lb.array() < ub.array()).all());
    _dbx = MatrixXd(_dim, 0);
    _dby = VectorXd(0, 1);
    _default_setting();
}
void MVMO::_default_setting()
{
    _max_eval     = _dim * 50;
    _num_init     = _dim * 5;
    _archive_size = 5;
    _fs_init      = 0.5;
    _fs_final     = 20;
    _m_init       = static_cast<size_t>(std::max(1.0, _dim / 6.0));
    _m_final      = static_cast<size_t>(std::max(1.0, _dim / 2.0));
    _delta_d0     = 0.25;

}
void MVMO::_init_archive()
{
    _archive_x    = MatrixXd(_dim, _archive_size);
    _archive_mean = VectorXd(_dim);
    _archive_s    = VectorXd::Zero(_dim, 1);
    _archive_d    = VectorXd::Ones(_dim, 1);
    _archive_s1   = VectorXd::Zero(_dim, 1);
    _archive_s2   = VectorXd::Zero(_dim, 1);
}
VectorXd MVMO::best_x() const { return _best_x; }
double MVMO::best_y() const { return _best_y; }
double MVMO::_run_func(const VectorXd& x)            // wrapper of the original objective function
{
    ++ _eval_counter;
    VectorXd sx = _scale_back(x);
    double   y  = _f(sx);
    if(y < _best_y)
    {
        _best_x = sx;
        _best_y = y;
    }
    // TODO: pre-allocating space
    _dbx.conservativeResize(Eigen::NoChange, _dbx.cols() + 1);
    _dby.conservativeResize(_dby.rows() + 1, Eigen::NoChange);
    _dbx.rightCols(1)     = x;
    _dby(_dby.size() - 1) = y;
    return y;
}
VectorXd MVMO::_run_func_batch(const Eigen::MatrixXd& xs)            // wrapper of the original objective function
{
    VectorXd ys(xs.cols());
    for(long i = 0; i < xs.cols(); ++i)
        ys(i) = _run_func(xs.col(i));
    return ys;
}
void MVMO::optimize()
{
#ifdef DEBUG_RAND_SEED
    srand(DEBUG_RAND_SEED);
#else
    srand(random_device{}());
#endif
    _init_archive();
    _initialize();
    while(_eval_counter < _max_eval)
    {
        optimize_one_step();
    }
}
void MVMO::optimize(const MatrixXd& guess)
{
#ifdef DEBUG_RAND_SEED
    srand(DEBUG_RAND_SEED);
#else
    srand(random_device{}());
#endif
    _init_archive();
    _initialize(guess);
    while(_eval_counter < _max_eval)
    {
        optimize_one_step();
    }
}
void MVMO::_initialize()
{
    if(_num_init < _archive_size)
        _num_init = _archive_size;
    MatrixXd init_x = MatrixXd(_dim, _num_init);
    init_x.setRandom();
    init_x = 0.5 * (init_x.array() + 1.0);
    _run_func_batch(init_x);
}
void MVMO::_initialize(const MatrixXd& guess)
{
    if(_num_init < _archive_size)
        _num_init = _archive_size;
    if(_num_init < (size_t)guess.cols())
        _num_init = guess.cols();
    MatrixXd scaled_guess(_dim, guess.cols());
    for(long i = 0; i < guess.cols(); ++i)
        scaled_guess.col(i) = _scale(guess.col(i));
    MatrixXd init_x = MatrixXd(_dim, _num_init);
    init_x.setRandom();
    init_x = 0.5 * (init_x.array() + 1.0);
    init_x.leftCols(scaled_guess.cols()) = scaled_guess;
    _run_func_batch(init_x);
}
void MVMO::optimize_one_step()
{
    if(std::isinf(_best_y))
        _best_x = _scale_back(_dbx.col(0));
    _archive();
    const size_t m               = _m();
    vector<size_t> dim_to_mutate = _pick_from_seq(_dim, m);
    VectorXd new_x               = _scale(_best_x);
#ifdef MVMO_DEBUG
    cout << "Eval: " << _eval_counter << endl;
    cout << "FS:   " << _fshape << endl;
    cout << "Besty: " << _best_y << endl;
    cout << _archive_x << endl;
    cout << "ArchS: " << _archive_s.transpose() << endl;
    cout << "ArchD: " << _archive_d.transpose() << endl;
#endif
    for(size_t idx : dim_to_mutate)
    {
        double xbar   = _archive_mean(idx);
        double s1     = _archive_s1(idx);
        double s2     = _archive_s2(idx);
        double x_star = _rand01();
        double hx     = _hfunc(xbar, s1, s2, x_star);
        double h0     = _hfunc(xbar, s1, s2, 0);
        double h1     = _hfunc(xbar, s1, s2, 1);
        new_x(idx)    = hx + (1.0 - h1 + h0) * x_star - h0;

        // XXX: not in the paper, but in ref.m(line 348-355)
        if(new_x(idx)  > 0.98 and _rand01() < 0.2)
            new_x(idx) = 1.0;
        else if(new_x(idx) < 0.02 and _rand01() < 0.2)
            new_x(idx) = 0.0;

#ifdef MVMO_DEBUG
        cout << "\tidx    :" << idx    << endl;
        cout << "\txbar   :" << xbar   << endl;
        cout << "\ts      :" << _archive_s(idx)     << endl;
        cout << "\td      :" << _archive_d(idx)     << endl;
        cout << "\ts1     :" << s1     << endl;
        cout << "\ts2     :" << s2     << endl;
        cout << "\tx_star :" << x_star << endl;
        cout << "\thx     :" << hx     << endl;
        cout << "\th0     :" << h0     << endl;
        cout << "\th1     :" << h1     << endl;
        cout << "\tnew_x(idx): " << new_x(idx) << endl;
        cout << "\t----------"   << endl;
#endif

    }
    double y = _run_func(new_x);
#ifdef MVMO_DEBUG
    cout << "new_x: " << new_x.transpose() << endl;
    cout << "new_y: " << y << endl;
    cout << "================" << endl;
#endif
}
void MVMO::_fs()
{
    const double alpha   = static_cast<double>(_eval_counter) / _max_eval;
    // XXX: alpha^2(the paper) vs alpha(the matlab code)
    const double fs_star = _fs_init + pow(alpha, 1) * (_fs_final - _fs_init);

    if(_rand01() > 0.5)
        _fshape = fs_star * (1 + _rand01());
    else
        _fshape = 1.0 + fs_star * (1 - _rand01()) * 0.25;
}

double MVMO::_m()
{
    const double alpha  = static_cast<double>(_eval_counter) / _max_eval;
    const double minit  = static_cast<double>(_m_init);
    const double mfinal = static_cast<double>(_m_final);
    const size_t mstar  = minit - pow(alpha, 1) * (minit - mfinal);
    const size_t m      = mfinal + _rand01() * (mstar - mfinal);
    return m;
}
vector<size_t> MVMO::_seq_idx(size_t n) const
{
    vector<size_t> idxs(n);
    for(size_t i = 0; i < n; ++i)
        idxs[i] = i;
    return idxs;
}

MatrixXd MVMO::_slice_matrix(const MatrixXd& m, const vector<size_t>& idxs) const
{
    assert((long)*max_element(idxs.begin(), idxs.end()) < m.cols());
    MatrixXd sm(m.rows(), idxs.size());
    for(size_t i = 0; i < idxs.size(); ++i)
        sm.col(i) = m.col(idxs[i]);
    return sm;
}
void MVMO::_archive()
{

    assert((long)_eval_counter == _dbx.cols());
    vector<size_t> idxs = _seq_idx(_eval_counter);
    std::nth_element(idxs.begin(), idxs.begin() + _archive_size, idxs.end(), [&](size_t i1, size_t i2)->bool{
        return _dby[i1] < _dby[i2];
    });
    for(size_t i = 0; i < _archive_size; ++i)
        _archive_x.col(i) = _dbx.col(idxs[i]);

    _fs();
    const double fs = _fshape;
    for(size_t i = 0; i < _dim; ++i)
    {
        const RowVectorXd variables = _archive_x.row(i);
        const Vector2d    mean_var  = _mean_var_noeq(variables);
        const double mean           = mean_var(0);
        const double var            = mean_var(1);
        const double s              = var < numeric_limits<double>::epsilon() ? _archive_s(i) : -1 * log(var) * fs;
        _archive_mean(i)            = mean;
        _archive_s(i)               = s;
        double s1= s, s2 = s;
        if(s > 0)
        {
            const double delta_d = (1 + _delta_d0) + 2 * _delta_d0 * (_rand01() - 0.5);
            if(s > _archive_d(i))
                _archive_d(i) *= delta_d;
            else
                _archive_d(i) /= delta_d;
            if(_rand01() < 0.5)
            {
                s1 = s;
                s2 = _archive_d(i);
            }
            else
            {
                s1 = _archive_d(i);
                s2 = s;
            }
        }
        _archive_s1(i) = std::isfinite(_best_y) ? s1 : 0.0;
        _archive_s2(i) = std::isfinite(_best_y) ? s2 : 0.0;
    }
}
std::vector<size_t> MVMO::_pick_from_seq(size_t n, size_t m)
{
    assert(m <= n);
    set<size_t> picked_set;
    uniform_int_distribution<size_t> i_distr(0, n - 1);
    while(picked_set.size() < m)
        picked_set.insert(i_distr(_engine));
    vector<size_t> picked_vec(m);
    std::copy(picked_set.begin(), picked_set.end(), picked_vec.begin());
    return picked_vec;
}
double MVMO::_rand01()
{
    return uniform_real_distribution<double>(0, 1)(_engine);
}
double MVMO::_hfunc(double xbar, double s1, double s2, double x) const
{
    return xbar * (1.0 - exp(-1 * x * s1)) + (1.0 - xbar) * exp(-1 * (1 - x) * s2);
}

Eigen::VectorXd MVMO::_scale(const Eigen::VectorXd& x) const       // scale from [lb, ub] to [0, 1]
{
    VectorXd a = (_ub - _lb).cwiseInverse();
    VectorXd b = -1 * _lb.cwiseProduct(a);
    return a.cwiseProduct(x) + b;
}
Eigen::VectorXd MVMO::_scale_back(const Eigen::VectorXd& x) const  // scale from [0, 1] to [lb, ub]
{
    VectorXd b = _lb;
    VectorXd a = _ub - _lb;
    return a.cwiseProduct(x) + b;
}
Vector2d MVMO::_mean_var_noeq(const RowVectorXd& xs) const
{
    double mean, var;
    vector<double> v(xs.size());
    for(size_t i = 0; i <  v.size(); ++i)
        v[i] = xs[i];
    std::sort(v.begin(), v.end());
    vector<double> vnoeq{v[0]};
    for(size_t i = 1; i < v.size(); ++i)
    {
        if(abs(v[i] - v[i-1]) > std::numeric_limits<double>::epsilon())
            vnoeq.push_back(v[i]);
    }

    if(vnoeq.size() == 1)
    {
        mean = vnoeq[0];
        var  = 0.0;
    }
    else
    {
        VectorXd vec = _convert(vnoeq);
        mean         = vec.mean();
        var          = (vec.array() - mean).array().square().mean();
    }
    Vector2d mean_var;
    mean_var << mean, var;
    return mean_var;
}
Eigen::VectorXd MVMO::_convert(std::vector<double>& x) const 
{
    VectorXd v(x.size());
    for(size_t i = 0; i < x.size(); ++i)
        v[i] = x[i];
    return v;
}
std::vector<double> MVMO::_convert(Eigen::VectorXd& v) const
{
    vector<double> x(v.size());
    for(size_t i = 0; i < x.size(); ++i)
        x[i] = v[i];
    return x;
}
