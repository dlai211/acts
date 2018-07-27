// This file is part of the Acts project.
//
// Copyright (C) 2016-2018 Acts project team
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once
#include <cmath>
#include <memory>
#include <type_traits>
#include "Acts/Propagator/AbortList.hpp"
#include "Acts/Propagator/ActionList.hpp"
#include "Acts/Propagator/detail/standard_abort_conditions.hpp"
#include "Acts/Utilities/Units.hpp"

namespace Acts {

namespace propagation {

  /// Propagation direction, relative to momentum
  enum Direction : int { backward = -1, forward = 1 };

  /// Result status of track parameter propagation
  enum struct Status { SUCCESS, FAILURE, UNSET, IN_PROGRESS, WRONG_DIRECTION };

  /// @brief Simple class holding result of propagation call
  ///
  /// @tparam TrackParameters Type of final track parameters
  ///
  /// @tparam ExResult        Parameter pack for additional propagation
  ///                         quantities
  ///
  template <typename TrackParameters, typename... ExResult>
  struct Result : private detail::Extendable<ExResult...>
  {
    /// Constructor from initial propagation status
    Result(Status s = Status::UNSET)
      : detail::Extendable<ExResult...>(), status(s)
    {
    }

    /// Accessor to additional propagation quantities
    using detail::Extendable<ExResult...>::get;

    /// Final track parameters
    std::unique_ptr<const TrackParameters> endParameters = nullptr;

    /// Propagation status
    Status status = Status::UNSET;

    /// Number of propagation steps that were carried out
    unsigned int steps = 0;

    /// Signed distance over which the parameters were propagated
    double pathLength = 0.;

    /// @brief Check the validity of the propagation result
    ///
    /// @return @c true if the final parameters are set and propagation status
    ///         is SUCCESS, otherwise @c false
    ///
    operator bool() const
    {
      return (endParameters && status == Status::SUCCESS);
    }
  };

  /// @brief Propagator for particles in a magnetic field
  ///
  /// @tparam Impl Implementation of the propagation algorithm
  ///
  /// This Propagator class serves as high-level steering code for propagating
  /// track parameters. The actual implementation of the propagation has to be
  /// implemented in the Impl object, which has to provide the following:
  ///
  /// - a function for performing a single propagation step
  /// - a type mapping for: initial track parameter type -> type of final track
  ///   parameters
  /// - a type mapping for: (initial track parameter type and destination
  ///   surface type) -> type of final track parameters
  /// - a type mapping for: initial track parameter type -> type of internal
  ///   cache object
  /// - a type mapping for: (initial track parameter type and destination
  ///   surface type) -> type of internal cache object
  ///
  template <typename Impl>
  class Propagator final
  {
  public:
    /// Type of cache object used by the propagation implementation
    typedef typename Impl::template cache_type<TrackParameters> cache_type;

    /// Typedef the path_limit_reached aborter
    typedef detail::path_limit_reached path_limit_reached;

    /// @brief Options for propagate() call
    ///
    /// @tparam Actions List of action types called after each
    ///                   propagation step with the current propagation
    ///                   cache
    ///
    /// @tparam Aborters  List of abort conditions tested after each
    ///                   propagation step using the current propagation
    ///                   cache
    ///
    template <typename Actions = ActionList<>, typename Aborters = AbortList<>>
    struct Options
    {

      /// Propagation direction
      Direction direction = forward;

      /// Maximum number of steps for one propagate() call
      unsigned int max_steps = 1000;

      /// Required tolerance to reach target (surface, pathlength)
      double target_tolerance = 1 * units::_um;

      /// Absolute maximum step size
      double max_step_size = 1 * units::_m;

      /// Absolute maximum path length
      double max_path_length = std::numeric_limits<double>::max();

      /// List of actions
      Actions action_list;

      /// List of abort conditions
      Aborters stop_conditions;
    };

    /// Constructor from implementation object
    explicit Propagator(Impl impl) : m_impl(std::move(impl)) {}

  private:
    /// @brief Helper struct determining the result's type
    ///
    /// @tparam TrackParameters Type of final track parameters
    /// @tparam Actions    List of propagation action types
    ///
    /// This helper struct provides type definitions to extract the correct
    /// propagation result type from a given TrackParameter type and an
    /// ActionList.
    ///
    template <typename TrackParameters, typename Actions>
    struct result_type_helper
    {
      /// @brief Propagation result type for an arbitrary list of additional
      ///        propagation results
      ///
      /// @tparam args Parameter pack specifying additional propagation results
      ///
      template <typename... args>
      using this_result_type = Result<TrackParameters, args...>;

      /// @brief Propagation result type derived from a given action list
      typedef typename Actions::template result_type<this_result_type> type;
    };

    /// @brief Short-hand type definition for propagation result derived from
    ///        an action list
    ///
    /// @tparam T       Type of the final track parameters
    /// @tparam Actions List of propagation action types
    ///
    template <typename T, typename Actions>
    using action_list_result_t = typename result_type_helper<T, Actions>::type;

    /// @brief Propagate track parameters - Private method with cache
    ///
    /// This function performs the propagation of the track parameters according
    /// to the internal implementation object until at least one abort condition
    /// is fulfilled, the destination surface is hit or the maximum number of
    /// steps/path length as given in the propagation options is reached.
    ///
    /// @note Does not (yet) convert into  the return_type of the propagation
    ///
    /// @tparam Result the result type for this propagation
    /// @tparam Actions       Type list of actions, type ActionList<>
    /// @tparam Aborters        Type list of abort conditions, type AbortList<>
    /// @tparam InteralAborter  additional internal aborters
    ///
    /// @param [in,out] Result of the propagation
    /// @param [in,out] Cache Stepper cache built/updated from the start
    /// parameters
    /// @param [in] Target Target surface of to propagate to
    /// @param [in] Options Propagation options
    ///
    /// @return Propagation Status
    template <typename Result,
              typename Actions,
              typename Aborters,
              typename InteralAborters>
    Status
    propagate_(Result&     result,
               cache_type& cache,
               const Options<Actions, Aborters>& options,
               const InteralAborters& internal_stop_conditions) const
    {
      // check with surface_abort if it exists
      if (internal_stop_conditions(result, cache)) return Status::FAILURE;

      // Propagation loop
      for (; result.steps < options.max_steps; ++result.steps) {
        // Perform a propagation step
        result.pathLength += m_impl.step(cache);
        // Call the actions, can (& will likely) modify cache
        options.action_list(cache, result);
        // Call the stop_conditions and the internal stop conditions
        // break condition triggered, but still count the step
        if (options.stop_conditions(result, cache)
            || internal_stop_conditions(result, cache)) {
          ++result.steps;
          break;
        }
      }
      return Status::IN_PROGRESS;
    }

  public:
    /// @brief Propagate track parameters
    ///
    /// This function performs the propagation of the track parameters using the
    /// internal stepper implementation, until at least one abort condition is
    /// fulfilled or the maximum number of steps/path length provided in the
    /// propagation options is reached.
    ///
    /// @tparam TrackParameters Type of initial track parameters to propagate
    /// @tparam Actions       Type list of actions, type ActionList<>
    /// @tparam Aborters        Type list of abort conditions, type AbortList<>
    ///
    /// @param [in] start   Initial track parameters to propagate
    /// @param [in] options Propagation options
    ///
    /// @return Propagation result containing the propagation status, final
    ///         track parameters, and output of actions (if they produce any)
    ///
    template <typename TrackParameters, typename Actions, typename Aborters>
    action_list_result_t<
        typename Impl::template return_parameter_type<TrackParameters>,
        Actions>
    propagate(const TrackParameters& start,
              const Options<Actions, Aborters>& options) const
    {

      // Type of track parameters produced by the propagation
      typedef typename Impl::template return_parameter_type<TrackParameters>
          return_parameter_type;

      // Type of the full propagation result, including output from actions
      typedef action_list_result_t<return_parameter_type, Actions> result_type;

      static_assert(std::is_copy_constructible<return_parameter_type>::value,
                    "return track parameter type must be copy-constructible");

      // Initialize the propagation result object
      result_type result(Status::IN_PROGRESS);

      // Initialize the internal propagation cache
      cache_type cache(start);
      cache.step_size = options.direction * options.max_step_size;

      // Internal Abort list
      AbortList<path_limit_reached> internal_aborters;
      // configure the aborter
      auto& path_limit_abort
          = internal_aborters.template get<path_limit_reached>();
      path_limit_abort.signed_path_limit
          = std::abs(options.max_path_length) * options.direction;
      path_limit_abort.tolerance = options.target_tolerance;

      // Perform the actual propagation & check it's outcome
      if (propagate_(result, cache, options, internal_aborters)
          != Status::IN_PROGRESS) {
        /// @todo screen output
      } else {
        /// Convert into the return type
        result.endParameters = std::make_unique<const return_parameter_type>(
            m_impl.convert(cache));
        result.status = Status::SUCCESS;
      }

      return result;
    }

    /// @brief Propagate track parameters - User method
    ///
    /// This function performs the propagation of the track parameters according
    /// to the internal implementation object until at least one abort condition
    /// is fulfilled, the destination surface is hit or the maximum number of
    /// steps/path length as given in the propagation options is reached.
    ///
    /// @tparam TrackParameters Type of initial track parameters to propagate
    /// @tparam Surface         Type of target surface
    /// @tparam Actions       Type list of actions
    /// @tparam Aborters        Type list of abort conditions
    ///
    /// @param [in] start Initial track parameters to propagate
    /// @param [in] target Target surface of to propagate to
    /// @param [in] options Propagation options
    ///
    /// @return Propagation result containing the propagation status, final
    ///         track parameters, and output of actions (if they produce any)
    template <typename TrackParameters,
              typename Surface,
              typename Actions,
              typename Aborters>
    action_list_result_t<
        typename Impl::template return_parameter_type<TrackParameters, Surface>,
        Actions>
    propagate(const TrackParameters& start,
              const Surface&         target,
              const Options<Actions, Aborters>& options) const
    {

      // Type of track parameters produced at the end of the propagation
      typedef typename Impl::template return_parameter_type<TrackParameters,
                                                            Surface>
          return_parameter_type;

      // Initialize the internal propagation cache
      cache_type cache(start, options.direction * options.max_step_size);

      // Type of the full propagation result, including output from actions
      typedef action_list_result_t<return_parameter_type, Actions> result_type;

      static_assert(std::is_copy_constructible<return_parameter_type>::value,
                    "return track parameter type must be copy-constructible");

      // Initialize the propagation result object
      result_type result(Status::IN_PROGRESS);

      // Target surface abort condition with tolerance
      typedef detail::surface_reached<Surface> target_reached;

      // Internal Abort list
      AbortList<target_reached, path_limit_reached> internal_aborters;
      // configure the aborters
      auto& at_target_abort = internal_aborters.template get<target_reached>();
      at_target_abort.surface   = &target;
      at_target_abort.direction = options.direction;
      at_target_abort.tolerance = options.target_tolerance;

      auto& path_limit_abort
          = internal_aborters.template get<path_limit_reached>();
      path_limit_abort.signed_path_limit
          = std::abs(options.max_path_length) * options.direction;
      path_limit_abort.tolerance = options.target_tolerance;

      // Perform the actual propagation
      if (propagate_(result, cache, options, internal_aborters)
          != Status::IN_PROGRESS) {
        // @todo: analyse and screen output
      } else {
        // Compute the final results and mark the propagation as successful
        result.endParameters = std::make_unique<const return_parameter_type>(
            m_impl.convert(cache, target));
        result.status = Status::SUCCESS;
      }
      return result;
    }

  private:
    /// implementation of propagation algorithm
    Impl m_impl;
  };

}  // namespace propagation

}  // namespace Acts