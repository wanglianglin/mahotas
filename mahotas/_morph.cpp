#include <algorithm>
#include <queue>
#include <vector>
#include <cstdio>
#include <limits>
#include <iostream>

#include "numpypp/array.hpp"
#include "numpypp/dispatch.hpp"

extern "C" {
    #include <Python.h>
    #include <numpy/ndarrayobject.h>
}

namespace{

const char TypeErrorMsg[] =
    "Type not understood. "
    "This is caused by either a direct call to _morph (which is dangerous: types are not checked!) or a bug in morph.py.\n";


template <typename T>
numpy::position central_position(const numpy::array_base<T>& array) {
    numpy::position res(array.raw_dims(), array.ndims());
    for (int i = 0, nd = array.ndims(); i != nd; ++i) res.position_[i] /= 2;
    return res;
}

template <typename T>
std::vector<numpy::position> neighbours(const numpy::aligned_array<T>& Bc, bool include_centre = false) {
    numpy::position centre = central_position(Bc);
    const unsigned N = Bc.size();
    typename numpy::aligned_array<T>::const_iterator startc = Bc.begin();
    std::vector<numpy::position> res;
    for (unsigned i = 0; i != N; ++i, ++startc) {
        if (!*startc) continue;
        if (startc.position() != centre || include_centre) {
            res.push_back(startc.position() - centre);
        }
    }
    return res;
}

template<typename T>
numpy::index_type margin_of(const numpy::position& position, const numpy::array_base<T>& ref) {
    numpy::index_type margin = std::numeric_limits<numpy::index_type>::max();
    for (int d = 0; d != ref.ndims(); ++d) {
        if (position[d] < margin) margin = position[d];
        int rmargin = ref.dim(d) - position[d] - 1;
        if (rmargin < margin) margin = rmargin;
   }
   return margin;
}


template<typename T>
void erode(numpy::aligned_array<T> res, numpy::array<T> array, numpy::aligned_array<T> Bc) {
    const unsigned N = res.size();
    const unsigned N2 = Bc.size();
    const numpy::position centre = central_position(Bc);
    typename numpy::aligned_array<T>::iterator rpos = res.begin();

    for (int i = 0; i != N; ++i, ++rpos) {
        bool on = true;
        typename numpy::aligned_array<T>::iterator startc = Bc.begin();
        for (int j = 0; j != N2; ++j, ++startc) {
            if (*startc) {
                numpy::position npos = rpos.position() + startc.position() - centre;
                if (array.validposition(npos) && !array.at(npos)) {
                    on = false;
                    break;
                }
            }
        }
        *rpos = on;
    }
}


PyObject* py_erode(PyObject* self, PyObject* args) {
    PyArrayObject* array;
    PyArrayObject* Bc;
    if (!PyArg_ParseTuple(args,"OO", &array, &Bc)) return NULL;
    PyArrayObject* res_a = (PyArrayObject*)PyArray_SimpleNew(array->nd,array->dimensions,PyArray_TYPE(array));
    if (!res_a) return NULL;
    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    erode<type>(numpy::aligned_array<type>(res_a),numpy::array<type>(array),numpy::aligned_array<type>(Bc));\

        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    return PyArray_Return(res_a);
}

template<typename T>
void dilate(numpy::aligned_array<T> res, numpy::array<T> array, numpy::aligned_array<T> Bc) {
    const unsigned N = res.size();
    const unsigned N2 = Bc.size();
    const numpy::position centre = central_position(Bc);

    typename numpy::array<T>::iterator pos = array.begin();
    for (int i = 0; i != N; ++i, ++pos) {
        if (*pos) {
            typename numpy::aligned_array<T>::iterator startc = Bc.begin();
            for (int j = 0; j != N2; ++j, ++startc) {
                if (*startc) {
                    numpy::position npos = pos.position() + startc.position() - centre;
                    if (res.validposition(npos)) {
                        res.at(npos) = *pos+*startc;
                    }
                }
            }
        }
    }

}

PyObject* py_dilate(PyObject* self, PyObject* args) {
    PyArrayObject* array;
    PyArrayObject* Bc;
    if (!PyArg_ParseTuple(args,"OO", &array, &Bc)) return NULL;
    PyArrayObject* res_a = (PyArrayObject*)PyArray_SimpleNew(array->nd,array->dimensions,PyArray_TYPE(array));
    if (!res_a) return NULL;
    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    dilate<type>(numpy::aligned_array<type>(res_a),numpy::array<type>(array),numpy::aligned_array<type>(Bc));\

        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    return PyArray_Return(res_a);
}

void close_holes(numpy::aligned_array<bool> ref, numpy::aligned_array<bool> f, numpy::aligned_array<bool> Bc) {
    std::fill_n(f.data(),f. size(), false);

    std::vector<numpy::position> stack;
    const unsigned N = ref.size();
    const std::vector<numpy::position> Bc_neighbours = neighbours(Bc);
    const unsigned N2 = Bc_neighbours.size();
    for (int d = 0; d != ref.ndims(); ++d) {
        if (ref.dim(d) == 0) continue;
        numpy::position pos;
        pos.nd_ = ref.ndims();
        for (int di = 0; di != ref.ndims(); ++di) pos.position_[di] = 0;

        for (int i = 0; i != N/ref.dim(d); ++i) {
            pos.position_[d] = 0;
            if (!ref.at(pos) && !f.at(pos)) {
                f.at(pos) = true;
                stack.push_back(pos);
            }
            pos.position_[d] = ref.dim(d) - 1;
            if (!ref.at(pos) && !f.at(pos)) {
                f.at(pos) = true;
                stack.push_back(pos);
            }

            for (int j = 0; j != ref.ndims() - 1; ++j) {
                if (j == d) ++j;
                if (pos.position_[j] < ref.dim(j)) {
                    ++pos.position_[j];
                    break;
                }
                pos.position_[j] = 0;
            }
        }
    }
    while (!stack.empty()) {
        numpy::position pos = stack.back();
        stack.pop_back();
        std::vector<numpy::position>::const_iterator startc = Bc_neighbours.begin();
        for (int j = 0; j != N2; ++j, ++startc) {
            numpy::position npos = pos + *startc;
            if (ref.validposition(npos) && !ref.at(npos) && !f.at(npos)) {
                f.at(npos) = true;
                stack.push_back(npos);
            }
        }
    }
    for (bool* start = f.data(), *past = f.data() + f.size(); start != past; ++ start) {
        *start = !*start;
    }
}

PyObject* py_close_holes(PyObject* self, PyObject* args) {
    PyArrayObject* ref;
    PyArrayObject* Bc;
    if (!PyArg_ParseTuple(args,"OO", &ref, &Bc)) {
        return NULL;
    }
    PyArrayObject* res_a = (PyArrayObject*)PyArray_SimpleNew(ref->nd,ref->dimensions,PyArray_TYPE(ref));
    if (!res_a) return NULL;
    if (PyArray_TYPE(ref) != NPY_BOOL || PyArray_TYPE(Bc) != NPY_BOOL) {
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    close_holes(numpy::aligned_array<bool>(ref), numpy::aligned_array<bool>(res_a), numpy::aligned_array<bool>(Bc));
    return PyArray_Return(res_a);
}

struct MarkerInfo {
    int cost;
    int idx;
    int position;
    int margin;
    MarkerInfo(int cost, int idx, int position, int margin)
        :cost(cost)
        ,idx(idx)
        ,position(position)
        ,margin(margin) {
        }
    bool operator < (const MarkerInfo& other) const {
        // We want the smaller argument to compare higher, so we reverse the order here:
        if (cost == other.cost) return idx > other.idx;
        return cost > other.cost;
    }
};

struct NeighbourElem {
    NeighbourElem(int delta, int margin, const numpy::position& delta_position)
        :delta(delta)
        ,margin(margin)
        ,delta_position(delta_position)
        { }
    int delta;
    int margin;
    numpy::position delta_position;
};

template<typename BaseType>
void cwatershed(numpy::aligned_array<BaseType> res, numpy::aligned_array<bool>* lines, numpy::aligned_array<BaseType> array, numpy::aligned_array<BaseType> markers, numpy::aligned_array<BaseType> Bc) {
    const unsigned N = res.size();
    const unsigned N2 = Bc.size();
    std::vector<NeighbourElem> neighbours;
    const numpy::position centre = central_position(Bc);
    typename numpy::aligned_array<BaseType>::iterator Bi = Bc.begin();
    for (int j = 0; j != N2; ++j, ++Bi) {
        if (*Bi) {
            numpy::position npos = Bi.position() - centre;
            int margin = 0;
            for (int d = 0; d != Bc.ndims(); ++d) {
                margin = std::max<int>(std::abs(npos[d]), margin);
            }
            int delta = markers.pos_to_flat(npos);
            if (!delta) continue;
            neighbours.push_back(NeighbourElem(delta, margin, npos));
        }
    }
    int idx = 0;

    std::vector<BaseType> cost(array.size());
    std::fill(cost.begin(), cost.end(), std::numeric_limits<BaseType>::max());

    std::vector<bool> status(array.size());
    std::fill(status.begin(), status.end(),false);

    std::priority_queue<MarkerInfo> hqueue;

    typename numpy::aligned_array<BaseType>::iterator mpos = markers.begin();
    for (int i =0; i != N; ++i, ++mpos) {
        if (*mpos) {
            assert(markers.validposition(mpos.position()));
            int margin = markers.size();
            for (int d = 0; d != markers.ndims(); ++d) {
                if (mpos.index(d) < margin) margin = mpos.index(d);
                int rmargin = markers.dim(d) - mpos.index(d) - 1;
                if (rmargin < margin) margin = rmargin;
            }
            hqueue.push(MarkerInfo(array.at(mpos.position()), idx++, markers.pos_to_flat(mpos.position()), margin));
            res.at(mpos.position()) = *mpos;
            cost[markers.pos_to_flat(mpos.position())] = array.at(mpos.position());
        }
    }

    while (!hqueue.empty()) {
        MarkerInfo next = hqueue.top();
        hqueue.pop();
        if (status[next.position]) continue;
        status[next.position] = true;
        for (std::vector<NeighbourElem>::const_iterator neighbour = neighbours.begin(), past = neighbours.end(); neighbour != past; ++neighbour) {
            numpy::index_type npos = next.position + neighbour->delta;
            int nmargin = next.margin - neighbour->margin;
            if (nmargin < 0) {
                numpy::position pos = markers.flat_to_pos(next.position);
                assert(markers.validposition(pos));
                numpy::position npos = pos + neighbour->delta_position;
                if (!markers.validposition(npos)) continue;


                // we are good, but the margin might have been wrong. Recompute
                nmargin = margin_of(npos, markers);
            }
            assert(npos < cost.size());
            if (!status[npos]) {
                BaseType ncost = array.at_flat(npos);
                if (ncost < cost[npos]) {
                    cost[npos] = ncost;
                    res.at_flat(npos) = res.at_flat(next.position);
                    hqueue.push(MarkerInfo(ncost, idx++, npos, nmargin));
                } else if (lines && res.at_flat(next.position) != res.at_flat(npos) && !lines->at_flat(npos)) {
                    lines->at_flat(npos) = true;
                }
            }
        }
    }
}

PyObject* py_cwatershed(PyObject* self, PyObject* args) {
    PyArrayObject* array;
    PyArrayObject* markers;
    PyArrayObject* Bc;
    int return_lines;
    if (!PyArg_ParseTuple(args,"OOOi", &array, &markers, &Bc,&return_lines)) {
        return NULL;
    }
    PyArrayObject* res_a = (PyArrayObject*)PyArray_SimpleNew(array->nd,array->dimensions,PyArray_TYPE(array));
    if (!res_a) return NULL;
    PyArrayObject* lines =  0;
    numpy::aligned_array<bool>* lines_a = 0;
    if (return_lines) {
        lines = (PyArrayObject*)PyArray_SimpleNew(array->nd,array->dimensions,PyArray_TYPE(array));
        if (!lines) return NULL;
        lines_a = new numpy::aligned_array<bool>(lines);
    }
    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    cwatershed<type>(numpy::aligned_array<type>(res_a),lines_a,numpy::aligned_array<type>(array),numpy::aligned_array<type>(markers),numpy::aligned_array<type>(Bc));
        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    if (return_lines) {
        delete lines_a;
        PyObject* ret_val = PyTuple_New(2);
        PyTuple_SetItem(ret_val,0,(PyObject*)res_a);
        PyTuple_SetItem(ret_val,1,(PyObject*)lines);
        return ret_val;
    }
    return PyArray_Return(res_a);
}

struct HitMissNeighbour {
    HitMissNeighbour(int delta, int value)
        :delta(delta)
        ,value(value)
        { }
    int delta;
    int value;
};

template <typename T>
void hitmiss(numpy::aligned_array<T> res, const numpy::aligned_array<T>& input, const numpy::aligned_array<T>& Bc) {
    typedef typename numpy::aligned_array<T>::iterator iterator;
    typedef typename numpy::aligned_array<T>::const_iterator const_iterator;
    const numpy::index_type N = input.size();
    const numpy::index_type N2 = Bc.size();
    const numpy::position centre = central_position(Bc);
    int Bc_margin = 0;
    for (int d = 0; d != Bc.ndims(); ++d) {
        int cmargin = Bc.dim(d)/2;
        if (cmargin > Bc_margin) Bc_margin = cmargin;
    }

    std::vector<HitMissNeighbour> neighbours;
    const_iterator Bi = Bc.begin();
    for (int j = 0; j != N2; ++j, ++Bi) {
        if (*Bi != 2) {
            numpy::position npos = Bi.position() - centre;
            int delta = input.pos_to_flat(npos);
            neighbours.push_back(HitMissNeighbour(delta, *Bi));
        }
    }

    int slack = 0;
    for (int i = 0; i != N; ++i) {
        while (!slack) {
            numpy::position cur = input.flat_to_pos(i);
            bool moved = false;
            for (int d = 0; d != input.ndims(); ++d) {
                int margin = std::min<int>(cur[d], input.dim(d) - cur[d] - 1);
                if (margin < Bc.dim(d)/2) {
                    int size = 1;
                    for (int dd = d+1; dd < input.ndims(); ++dd) size *= input.dim(dd);
                    for (int j = 0; j != size; ++j) {
                        res.at_flat(i++) = 0;
                        if (i == N) return;
                    }
                    moved = true;
                }
            }
            if (!moved) slack = input.dim(input.ndims() - 1) - Bc.dim(input.ndims() - 1) + 1;
        }
        --slack;
        T value = 1;
        for (std::vector<HitMissNeighbour>::const_iterator neighbour = neighbours.begin(), past = neighbours.end();
            neighbour != past;
            ++neighbour) {
            if (input.at_flat(i + neighbour->delta) != neighbour->value) {
                value = 0;
                break;
            }
        }
        res.at_flat(i) = value;
    }
}

PyObject* py_hitmiss(PyObject* self, PyObject* args) {
    PyArrayObject* array;
    PyArrayObject* Bc;
    PyArrayObject* res_a;
    if (!PyArg_ParseTuple(args, "OOO", &array, &Bc, &res_a)) {
        return NULL;
    }
    Py_INCREF(res_a);

    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    hitmiss<type>(numpy::aligned_array<type>(res_a), numpy::aligned_array<type>(array), numpy::aligned_array<type>(Bc));
        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    return PyArray_Return(res_a);
}

PyMethodDef methods[] = {
  {"dilate",(PyCFunction)py_dilate, METH_VARARGS, NULL},
  {"erode",(PyCFunction)py_erode, METH_VARARGS, NULL},
  {"close_holes",(PyCFunction)py_close_holes, METH_VARARGS, NULL},
  {"cwatershed",(PyCFunction)py_cwatershed, METH_VARARGS, NULL},
  {"hitmiss",(PyCFunction)py_hitmiss, METH_VARARGS, NULL},
  {NULL, NULL,0,NULL},
};

} // namespace
extern "C"
void init_morph()
  {
    import_array();
    (void)Py_InitModule("_morph", methods);
  }

