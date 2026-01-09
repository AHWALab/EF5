/*
 * EF5 Core Library - Python Bindings
 *
 * pybind11 bindings for exposing libef5core to Python.
 * Provides NumPy-compatible interfaces for CREST and Kinematic routing.
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ef5core/ef5core.hpp"

namespace py = pybind11;

// ============================================================================
// Helper functions for NumPy array conversions
// ============================================================================

template <typename T> T *numpy_data(py::array_t<T> &arr) {
  return static_cast<T *>(arr.mutable_data());
}

template <typename T> const T *numpy_data_const(const py::array_t<T> &arr) {
  return static_cast<const T *>(arr.data());
}

// ============================================================================
// CREST Bindings
// ============================================================================

void bind_crest(py::module_ &m) {
  using namespace ef5::crest;

  auto crest_mod = m.def_submodule("crest", "CREST water balance model");

  // Parameters struct
  py::class_<Parameters>(crest_mod, "Parameters",
                         "CREST model parameters per grid cell")
      .def(py::init<>())
      .def_readwrite("wm", &Parameters::wm, "Maximum soil water capacity (mm)")
      .def_readwrite("b", &Parameters::b,
                     "B exponent for variable infiltration")
      .def_readwrite("im", &Parameters::im, "Impervious area fraction (0-1)")
      .def_readwrite("ke", &Parameters::ke, "Evaporation coefficient")
      .def_readwrite("fc", &Parameters::fc,
                     "Field capacity infiltration rate (mm/hr)")
      .def_readwrite("iwu", &Parameters::iwu, "Initial water content (% of WM)")
      .def_readwrite("ksat", &Parameters::ksat,
                     "Saturated hydraulic conductivity (mm/hr)")
      .def("__repr__", [](const Parameters &p) {
        return "<crest.Parameters wm=" + std::to_string(p.wm) +
               " b=" + std::to_string(p.b) + ">";
      });

  // State struct
  py::class_<State>(crest_mod, "State", "CREST model state per grid cell")
      .def(py::init<>())
      .def_readwrite("soil_moisture", &State::soil_moisture,
                     "Current soil moisture (mm)")
      .def_readwrite("excess_overland", &State::excess_overland,
                     "Overland excess (mm)")
      .def_readwrite("excess_interflow", &State::excess_interflow,
                     "Interflow excess (mm)")
      .def_readwrite("actual_et", &State::actual_et,
                     "Actual ET this timestep (mm)")
      .def("__repr__", [](const State &s) {
        return "<crest.State soil_moisture=" + std::to_string(s.soil_moisture) +
               "mm>";
      });

  // Diagnostics struct
  py::class_<Diagnostics>(
      crest_mod, "Diagnostics",
      "Diagnostic information from water balance computation")
      .def(py::init<>())
      .def_readonly("cells_processed", &Diagnostics::cells_processed)
      .def_readonly("sm_clamped_low", &Diagnostics::sm_clamped_low)
      .def_readonly("sm_clamped_high", &Diagnostics::sm_clamped_high)
      .def_readonly("runoff_clamped", &Diagnostics::runoff_clamped)
      .def_readonly("infiltration_clamped", &Diagnostics::infiltration_clamped)
      .def_readonly("nan_values_fixed", &Diagnostics::nan_values_fixed)
      .def("has_warnings", &Diagnostics::has_warnings,
           "Check if any warnings occurred")
      .def("__repr__", [](const Diagnostics &d) {
        return "<crest.Diagnostics processed=" +
               std::to_string(d.cells_processed) +
               " warnings=" + (d.has_warnings() ? "yes" : "no") + ">";
      });

  // Function: water_balance_grid
  crest_mod.def(
      "water_balance_grid",
      [](std::vector<Parameters> &params, std::vector<State> &states,
         py::array_t<float, py::array::c_style> &precip,
         py::array_t<float, py::array::c_style> &pet,
         float step_hours) -> py::tuple {
        size_t n_cells = params.size();

        // Validate sizes
        if (states.size() != n_cells ||
            static_cast<size_t>(precip.size()) != n_cells ||
            static_cast<size_t>(pet.size()) != n_cells) {
          throw std::runtime_error("Array sizes must match");
        }

        // Allocate output arrays
        py::array_t<float> fast_flow(n_cells);
        py::array_t<float> slow_flow(n_cells);
        py::array_t<float> soil_moisture(n_cells);

        // Call C++ function
        Diagnostics diag = water_balance_grid(
            params.data(), states.data(),
            static_cast<const float *>(precip.data()),
            static_cast<const float *>(pet.data()), n_cells, step_hours,
            numpy_data(fast_flow), numpy_data(slow_flow),
            numpy_data(soil_moisture));

        return py::make_tuple(fast_flow, slow_flow, soil_moisture, diag);
      },
      py::arg("params"), py::arg("states"), py::arg("precip"), py::arg("pet"),
      py::arg("step_hours"),
      R"doc(
        Compute water balance for entire grid.
        
        Parameters
        ----------
        params : list of crest.Parameters
            Model parameters, one per cell
        states : list of crest.State
            Model states (updated in-place)
        precip : ndarray of float32
            Precipitation (mm/hr)
        pet : ndarray of float32
            Potential ET (mm/hr)
        step_hours : float
            Timestep in hours
            
        Returns
        -------
        tuple of (fast_flow, slow_flow, soil_moisture, diagnostics)
        )doc");

  // Function: initialize_states
  crest_mod.def(
      "initialize_states",
      [](std::vector<Parameters> &params, std::vector<State> &states) {
        size_t n_cells = params.size();
        initialize_states(params.data(), states.data(), n_cells);
      },
      py::arg("params"), py::arg("states"),
      "Initialize states from parameters (uses IWU for soil moisture)");

  // Function: validate_parameters
  crest_mod.def(
      "validate_parameters",
      [](std::vector<Parameters> &params) -> Diagnostics {
        size_t n_cells = params.size();
        return validate_parameters(params.data(), n_cells);
      },
      py::arg("params"), "Validate and clamp parameters to valid ranges");

  // State access functions
  crest_mod.def(
      "get_states_soil_moisture",
      [](std::vector<State> &states) -> py::array_t<float> {
        size_t n_cells = states.size();
        py::array_t<float> out(n_cells);
        get_states_soil_moisture(states.data(), n_cells, numpy_data(out));
        return out;
      },
      py::arg("states"), "Extract soil moisture values for saving to file");

  crest_mod.def(
      "set_states_soil_moisture",
      [](std::vector<State> &states, py::array_t<float> &sm) {
        size_t n_cells = states.size();
        set_states_soil_moisture(states.data(), n_cells, numpy_data_const(sm));
      },
      py::arg("states"), py::arg("soil_moisture"),
      "Load soil moisture values from file");

  crest_mod.def(
      "get_actual_et",
      [](std::vector<State> &states) -> py::array_t<float> {
        size_t n_cells = states.size();
        py::array_t<float> out(n_cells);
        get_actual_et(states.data(), n_cells, numpy_data(out));
        return out;
      },
      py::arg("states"), "Get actual ET values from last timestep");

  // Helper to create parameter/state vectors
  crest_mod.def(
      "make_params",
      [](size_t n_cells) { return std::vector<Parameters>(n_cells); },
      py::arg("n_cells"), "Create a list of Parameters");

  crest_mod.def(
      "make_states", [](size_t n_cells) { return std::vector<State>(n_cells); },
      py::arg("n_cells"), "Create a list of States");
}

// ============================================================================
// Kinematic Bindings
// ============================================================================

void bind_kinematic(py::module_ &m) {
  using namespace ef5::kinematic;

  auto kw_mod = m.def_submodule("kinematic", "Kinematic wave routing model");

  // Parameters struct
  py::class_<Parameters>(kw_mod, "Parameters",
                         "Kinematic wave routing parameters")
      .def(py::init<>())
      .def_readwrite("alpha", &Parameters::alpha,
                     "Channel kinematic wave parameter")
      .def_readwrite("alpha0", &Parameters::alpha0,
                     "Overland kinematic wave parameter")
      .def_readwrite("beta", &Parameters::beta,
                     "Channel cross-section exponent")
      .def_readwrite("threshold", &Parameters::threshold,
                     "FAC threshold for channel cells")
      .def_readwrite("leak_i", &Parameters::leak_i, "Interflow leak rate (0-1)")
      .def_readwrite("under", &Parameters::under,
                     "Undersurface flow speed factor")
      .def_readwrite("isu", &Parameters::isu, "Initial interflow storage (mm)")
      .def("__repr__", [](const Parameters &p) {
        return "<kinematic.Parameters alpha=" + std::to_string(p.alpha) + ">";
      });

  // State struct
  py::class_<State>(kw_mod, "State", "Kinematic wave state per grid cell")
      .def(py::init<>())
      .def_readwrite("prev_channel_q", &State::prev_channel_q)
      .def_readwrite("prev_overland_q", &State::prev_overland_q)
      .def_readwrite("interflow_reservoir", &State::interflow_reservoir)
      .def("__repr__", [](const State &s) {
        return "<kinematic.State prev_q=" + std::to_string(s.prev_channel_q) +
               ">";
      });

  // Helper to create vectors
  kw_mod.def(
      "make_params",
      [](size_t n_cells) { return std::vector<Parameters>(n_cells); },
      py::arg("n_cells"), "Create a list of Parameters");

  kw_mod.def(
      "make_states", [](size_t n_cells) { return std::vector<State>(n_cells); },
      py::arg("n_cells"), "Create a list of States");

  // initialize_states
  kw_mod.def(
      "initialize_states",
      [](std::vector<Parameters> &params, std::vector<State> &states) {
        size_t n_cells = params.size();
        initialize_states(params.data(), states.data(), n_cells);
      },
      py::arg("params"), py::arg("states"));

  // reset_incoming_flows
  kw_mod.def(
      "reset_incoming_flows",
      [](std::vector<State> &states) {
        size_t n_cells = states.size();
        reset_incoming_flows(states.data(), n_cells);
      },
      py::arg("states"),
      "Reset accumulated incoming flows before each timestep");

  // State access functions
  kw_mod.def(
      "get_all_states",
      [](std::vector<State> &states) -> py::array_t<float> {
        size_t n_cells = states.size();
        py::array_t<float> out({n_cells, size_t(3)});
        get_all_states(states.data(), n_cells, numpy_data(out));
        return out;
      },
      py::arg("states"),
      "Get all states as (n_cells, 3) array [prev_channel_q, prev_overland_q, "
      "interflow_res]");

  kw_mod.def(
      "set_all_states",
      [](std::vector<State> &states, py::array_t<float> &data) {
        size_t n_cells = states.size();
        set_all_states(states.data(), n_cells, numpy_data_const(data));
      },
      py::arg("states"), py::arg("data"),
      "Set all states from (n_cells, 3) array");
}

// ============================================================================
// GridCell and RoutingTopology Bindings
// ============================================================================

void bind_types(py::module_ &m) {
  using namespace ef5;

  // GridCell struct
  py::class_<GridCell>(
      m, "GridCell", "Grid cell with topographic and connectivity information")
      .def(py::init<>())
      .def_readwrite("x", &GridCell::x, "Column index")
      .def_readwrite("y", &GridCell::y, "Row index")
      .def_readwrite("slope", &GridCell::slope, "Terrain slope")
      .def_readwrite("area", &GridCell::area, "Cell area (km²)")
      .def_readwrite("contrib_area", &GridCell::contrib_area,
                     "Contributing area (km²)")
      .def_readwrite("hor_len", &GridCell::hor_len, "Horizontal length (m)")
      .def_readwrite("river_len", &GridCell::river_len, "River length (m)")
      .def_readwrite("relief", &GridCell::relief, "Relief (m)")
      .def_readwrite("fac", &GridCell::fac, "Flow accumulation count")
      .def_readwrite("downstream_index", &GridCell::downstream_index,
                     "Downstream cell index")
      .def_readwrite("is_channel", &GridCell::is_channel, "Is channel cell")
      .def("__repr__", [](const GridCell &c) {
        return "<GridCell (" + std::to_string(c.x) + "," + std::to_string(c.y) +
               ") fac=" + std::to_string(c.fac) + ">";
      });

  // RoutingTopology struct
  py::class_<RoutingTopology>(
      m, "RoutingTopology",
      "Routing topology for parallel kinematic wave computation")
      .def(py::init<>())
      .def_readonly("level_starts", &RoutingTopology::level_starts)
      .def_readonly("sorted_indices", &RoutingTopology::sorted_indices)
      .def_readonly("num_levels", &RoutingTopology::num_levels)
      .def(
          "compute_from_grid",
          [](RoutingTopology &self, py::array_t<GridCell> &cells) {
            self.compute_from_grid(static_cast<const GridCell *>(cells.data()),
                                   static_cast<size_t>(cells.size()));
          },
          py::arg("cells"), "Compute routing topology from grid connectivity")
      .def("__repr__", [](const RoutingTopology &t) {
        return "<RoutingTopology levels=" + std::to_string(t.num_levels) + ">";
      });

  // Helper to create GridCell array
  m.def(
      "make_grid_cells",
      [](size_t n_cells) { return py::array_t<GridCell>(n_cells); },
      py::arg("n_cells"), "Create an array of GridCells");
}

// ============================================================================
// Module Definition
// ============================================================================

PYBIND11_MODULE(_ef5core, m) {
  m.doc() = R"doc(
        EF5 Core Library - Python bindings
        
        Low-level interface to EF5 computational kernels.
        This module provides direct access to CREST water balance
        and Kinematic wave routing algorithms.
        
        For high-level usage, see the ef5py package.
    )doc";

  // Version info
  m.attr("__version__") = ef5::get_version();

  // Thread control
  m.def("get_num_threads", &ef5::get_num_threads,
        "Get number of OpenMP threads");
  m.def("set_num_threads", &ef5::set_num_threads, py::arg("n"),
        "Set number of OpenMP threads");

  // Bind submodules
  bind_types(m);
  bind_crest(m);
  bind_kinematic(m);
}
