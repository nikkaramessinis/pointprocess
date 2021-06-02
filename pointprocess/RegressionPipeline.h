//
// Created by Andrea Bonvini on 21/04/21.
//

#ifndef POINTPROCESS_REGRESSIONPIPELINE_H
#define POINTPROCESS_REGRESSIONPIPELINE_H

#include "InterEventDistributions.h"
#include "Eigen/Core"
#include "optimizers/BaseOptimizer.h"
#include "optimizers/InverseGaussianOptimizer.h"
#include "optimizers/GaussianOptimizer.h"
#include "optimizers/LogNormalOptimizer.h"
#include "WeightsProducer.h"
#include <utility>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cmath>
#include <iostream>

struct Stats{
    double ksDistance = 0.0;
    double percOut = 0.0;
    double autoCorr = 0.0;
    Stats() = default;
    Stats(double ksDistance, double percOut, double autoCorr){
        this->ksDistance = ksDistance;
        this->percOut = percOut;
        this->autoCorr = autoCorr;
    }
};

struct PointProcessResult{ // TODO: add Documentation
    std::vector<std::shared_ptr<RegressionResult>> results;
    std::vector<double> taus;
    PointProcessDistributions distribution;
    unsigned char AR_ORDER;
    bool hasTheta0;
    double windowLength;
    double delta;
    double t0;
    Stats stats;
    PointProcessResult(
            std::vector<std::shared_ptr<RegressionResult>> results,
            std::vector<double> taus,
            PointProcessDistributions distribution,
            unsigned char AR_ORDER,
            bool hasTheta0,
            double windowLength,
            double delta,
            double t0,
            Stats stats
    ){
        this->results = std::move(results);
        this->taus = std::move(taus);
        this->distribution = distribution;
        this->AR_ORDER = AR_ORDER;
        this->hasTheta0 = hasTheta0;
        this->windowLength = windowLength;
        this->delta = delta;
        this->t0 = t0;
        this->stats = stats;
    }
};


static Stats computeStats(std::vector<double>& taus){
    std::vector<double> rescaledTimes;
    Eigen::VectorXd z(taus.size());
    for (long i = 0 ; i < taus.size(); i++){
        z[i] = taus[i];
    }
    z = - z;
    z = 1.0 - z.array().exp();
    std::sort(z.data(), z.data() + z.size());
    auto lin = Eigen::VectorXd::LinSpaced(z.size(),0.0,1.0);
    auto lu = Eigen::VectorXd::LinSpaced(z.size(),1.36 / sqrt(z.size()), 1.0 + 1.36 / sqrt(z.size()));
    auto ll = Eigen::VectorXd::LinSpaced(z.size(),-1.36 / sqrt(z.size()), 1.0 - 1.36 / sqrt(z.size()));
    double ksDistance = (z.array() - lin.array()).abs().maxCoeff() / sqrt(2.0);
    double percOut = 0.0;
    double autoCorr = 0.0;
    for (long i = 0; i < z.size(); i++){
        percOut += (double) z[i] < ll[i] || z[i] > lu[i];
    }
    percOut = percOut / ((double) z.size());
    return Stats(ksDistance, percOut, autoCorr);

}


PipelineSetup static getPipelineSetup(const std::vector<double>& events, bool rc, bool hasTheta0_, unsigned char AR_ORDER_, double windowLength, double delta, unsigned long maxIter, WeightsProducer weightsProducer){
    /**
     * This function returns a PipelineSetup object containing a series of useful parameters for a fullRegression(),
     * such as:
     * last_event_index:
     *     index of the last event within the first time window
     *     e.g. if events = [0.0, 1.3, 2.1, 3.2, 3.9, 4.5] and window_length = 3.5 then last_event_index = 3
     *     (since events[3] = 3.2 and events[4] =3.9)
     * bins:
     *     total number of bins we can discretize our events in (given our time_resolution)
     * bins_in_window:
     *     number of bins in a single time window.
     **/
    // Consistency check
    if (events[ events.size() -1] < windowLength){
        throw std::invalid_argument("The window length is too wide.");
    }
    // Find the index of the last event within window_length
    unsigned long last_event_index = 0;
    for(unsigned long index = 0; index < events.size(); index++){
        if (events[index] > windowLength){
            last_event_index = index - 1;
            break;
        }
    }
    // Find total number of time bins
    auto bins = (unsigned long) std::ceil(events[events.size() -1] / delta);
    auto bins_in_window = (unsigned long) (windowLength / delta);

    return PipelineSetup(delta, events, rc, hasTheta0_, AR_ORDER_, last_event_index, bins, bins_in_window, maxIter, weightsProducer);
}


static void computeTaus(std::vector<double>& taus ,const std::vector<std::shared_ptr<RegressionResult>>& results, const PipelineSetup& setup){

    double currentTime;
    bool eventHappened;
    unsigned long last_event_index = setup.last_event_index;
    double dt;
    double intL = 0.0;
    double pr = 0.0;
    bool wait = true;

    unsigned long offset = setup.bins_in_window;
    for (unsigned long bin_index = offset; bin_index <= setup.bins; bin_index++) {
        currentTime = (double) bin_index * setup.delta;
        eventHappened = setup.events[last_event_index + 1] <= currentTime;
        if (eventHappened){
            last_event_index++;
            dt = setup.events[last_event_index] - (currentTime - setup.delta);
            intL = intL + dt * pr;
            if (!wait) {
                taus.push_back(intL);
            }
            // we set wait to false when we observe the FIRST event after the starting timeWindow, this way we'll
            // be able to compute the integral of lambda (i.e. tau) only for fully observed inter-event intervals.
            /* e.g.
             *              1  2    3   4    5    6  7
             * events:    --|--|----|---|----|----|--|
             *   bins:    ----------------------------
             * timeWindow:  \_____________/XX       <- WRONG
             *                 \_____________/      <- RIGHT
             *  We have to wait the first observable event (5) in order to compute a reliable estimate
             *  of the first tau (integral of lambda)
             */
            wait = false;
            intL = 0.0;
        }
        else{
            intL = intL + setup.delta * results[bin_index - offset]->lambda;
        }
        if (bin_index <= results.size()){
            pr =  results[bin_index - offset]->lambda;
        }
    }
}


class RegressionPipeline{
public:
    PointProcessDistributions distribution;
    unsigned char AR_ORDER;
    bool hasTheta0;
    RegressionPipeline(
            PointProcessDistributions distribution,
            unsigned char AR_ORDER,
            bool hasTheta0
            ){
        /******************************************************************
         * Parameters:
         *     distribution: One of the enumeration values of PointProcessDistributions, either
         *         1) InverseGaussian
         *         2) LogNormal
         *         3) Gaussian
         *     AR_ORDER_: AR order to use for the estimation of the first moment of the given distribution.
         *     hasTheta0_: if the AR model takes account for a mean/theta0 parameter.
         *
         *****************************************************************/
        this->distribution = distribution;
        this->AR_ORDER = AR_ORDER;
        this->hasTheta0 = hasTheta0;
    }

    [[nodiscard]] PointProcessResult fullRegression(
            const std::vector<double>& events_times,
            double windowLength = 60.0,
            double delta = 0.005,
            bool rightCensoring = true,
            unsigned int maxIter = 1000,
            WeightsProducer weightsProducer = WeightsProducer()
            ) const{
        /**************************************************************************************************************
         * This function implements part of the pipeline suggested by Riccardo Barbieri, Eric C. Matten,
         * Abdul Rasheed A. Alabi and Emery N. Brown in the paper:
         * "A point-process model of human heartbeat intervals:
         *  new definitions of heart rate and heart rate variability".
         *
         * Check the various Optimizer objects to see how the optimization process is carried out for the different
         * distributions.
         *
         * Parameters:
         *     events_times: event times expressed in seconds.
         *     windowLength: time window used for the local likelihood maximization.
         *     delta: how much the local likelihood time interval is shifted to compute the next parameter update,
         *            be careful: time_resolution must be little enough s.t. at most ONE event can happen in each
         *            time bin. Moreover the smaller it is the better since we use it to approximate the integral
         *            of the lambda function.
         *     rightCensoring: if the regression should take into account right-censoring or not, if true we should have
         *                     more accurate estimates for the first and second moment of the selected distribution.
         *     maxIter: maximum number of iterations allowed for each optimization procedure.
         ************************************************************************************************************/

        // We want the first event to be at time 0 (events = events_times - events_times[0])
        std::vector<double> events = events_times;
        double t0 = events_times[0];
        std::transform(events_times.begin(), events_times.end(), events.begin(),[&](auto& value){ return value - t0;});

        auto pipelineSetup = getPipelineSetup(events, rightCensoring, hasTheta0, AR_ORDER, windowLength, delta,
                                              maxIter, weightsProducer);

        // TODO: remove switch statement and factorize...
        switch (this->distribution) {
            case PointProcessDistributions::InverseGaussian: {
                auto optimizer = InverseGaussianOptimizer();
                auto results = optimizer.train(pipelineSetup);
                std::vector<double> taus;
                computeTaus(taus, results, pipelineSetup);
                auto stats = computeStats(taus);
                return PointProcessResult(results, taus, this->distribution, this->AR_ORDER, this-> hasTheta0, windowLength, delta, t0, stats);
            }
            case PointProcessDistributions::LogNormal: {
                auto optimizer = LogNormalOptimizer();
                auto results = optimizer.train(pipelineSetup);
                std::vector<double> taus;
                computeTaus(taus, results, pipelineSetup);
                auto stats = computeStats(taus);
                return PointProcessResult(results, taus, this->distribution, this->AR_ORDER, this-> hasTheta0, windowLength, delta, t0, stats);
            }
            case PointProcessDistributions::Gaussian: {
                auto optimizer = GaussianOptimizer();
                auto results = optimizer.train(pipelineSetup);
                std::vector<double> taus;
                computeTaus(taus, results, pipelineSetup);
                auto stats = computeStats(taus);
                return PointProcessResult(results, taus, this->distribution, this->AR_ORDER, this-> hasTheta0, windowLength, delta, t0, stats);
            }
            default:
                throw std::logic_error("Please, insert a valid InterEvent distribution.");

        }
    }
};


#endif //POINTPROCESS_REGRESSIONPIPELINE_H

