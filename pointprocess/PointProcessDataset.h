//
// Created by Andrea Bonvini on 20/04/21.
//

#ifndef POINTPROCESS_POINTPROCESSDATASET_H
#define POINTPROCESS_POINTPROCESSDATASET_H

#include <utility>
#include <vector>
#include <deque>
#include <numeric>
#include <Eigen/Core>
#include "WeightsProducer.h"

#include <iostream>


void static toeplitz(const std::vector<double>& col, const std::vector<double>& row, Eigen::MatrixXd& toep){
    assert(col[0] == row[0]);
    for(long i = 0; i != col.size(); i++) {
        for(long j = 0; j != row.size(); j++) {
            if (j >= i){
                // Upper diagonal
                toep(i,j) = row[j - i];
            }
            else{
                // Lower diagonal
                toep(i,j) = col[i - j];
            }
        }
    }
}

class PointProcessDataset{
public:
    unsigned char N_SAMPLES;
    unsigned char AR_ORDER;
    bool hasTheta0;
    Eigen::MatrixXd xn;
    Eigen::VectorXd wn;
    Eigen::VectorXd eta;
    Eigen::VectorXd xt;
    double wt;
    PointProcessDataset(
            unsigned char N_SAMPLES_,
            unsigned char AR_ORDER_,
            bool hasTheta0_,
            Eigen::MatrixXd xn_,
            Eigen::VectorXd wn_,
            Eigen::VectorXd eta_,
            Eigen::VectorXd xt_,
            double wt_
    ){
        N_SAMPLES = N_SAMPLES_;
        AR_ORDER = AR_ORDER_;
        hasTheta0 = hasTheta0_;
        xn = std::move(xn_);
        wn = std::move(wn_);
        eta = std::move(eta_);
        xt = std::move(xt_);
        wt = wt_;
    }

    static PointProcessDataset load(
            std::deque<double> events_times,
            unsigned char AR_ORDER_,
            bool hasTheta0_,
            WeightsProducer& weightsProducer,
            double current_time = 0.0
    ){
        std::deque<double> inter_events_times;
        inter_events_times = events_times;
        std::adjacent_difference(inter_events_times.begin(),inter_events_times.end(),inter_events_times.begin());
        inter_events_times.pop_front();
        // We now define wn, i.e. the intervals we have to predict once we build our AR model.
        std::vector<double> wn_v = std::vector<double>(inter_events_times.begin() + AR_ORDER_, inter_events_times.end());
        // Let's copy it into an Eigen Vector object.
        Eigen::VectorXd wn_ = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(wn_v.data(), (long) wn_v.size());
        // We now have to build a matrix xn s.t. for i = 0, ..., len(inter_events_times)-p-1 the i_th element of xn will be
        // xn[i] = [1, inter_events_times[i + p - 1], inter_events_times[i + p - 2], ..., rr[i]]
        // a = inter_events_times[p - 1 : -1]
        std::vector<double> a = std::vector<double>(inter_events_times.begin() + AR_ORDER_ - 1, inter_events_times.end() - 1);
        // b = inter_events_times[p - 1 :: -1]
        std::vector<double> b = std::vector<double>(inter_events_times.begin(), inter_events_times.begin() + AR_ORDER_);
        std::reverse(b.begin(), b.end());
        // xn = toeplitz(a, b)
        Eigen::MatrixXd xn_tmp(a.size(),b.size());
        toeplitz(a,b,xn_tmp);
        // Note that the 1 at the beginning of each row is added only if the hasTheta0 parameter is set to True.
        Eigen::MatrixXd xn_(xn_tmp.rows(),xn_tmp.cols() + hasTheta0_);

        if (hasTheta0_){
            auto ones = Eigen::MatrixXd::Ones( (long) xn_tmp.rows(),1);
            xn_ << ones, xn_tmp;
        }
        else{
            xn_ = xn_tmp;
        }
        // xt = inter_events_times[-p:][::-1]
        std::vector<double> xt_v = std::vector<double>(inter_events_times.end() - AR_ORDER_, inter_events_times.end());
        std::reverse(xt_v.begin(), xt_v.end());
        Eigen::VectorXd xt_tmp = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(xt_v.data(), (long) xt_v.size());
        Eigen::VectorXd xt_(AR_ORDER_ + hasTheta0_);
        if (hasTheta0_) {
            auto one = Eigen::VectorXd::Ones((long) 1);
            xt_ << one, xt_tmp;
        }
        else
            xt_ = xt_tmp;
        if (current_time == 0.0)
            current_time = events_times[events_times.size() - 1];

        if (current_time < events_times[events_times.size() - 1]){
            std::cout << "Current Time: " << current_time << std::endl;
            std::cout << "Events Times:" << std::endl;
            for (auto& el : events_times){
                std::cout << el << std::endl;
            }
        }
        assert(current_time >= events_times[events_times.size() - 1]);
        double wt_ = current_time - events_times[events_times.size() - 1];
        // target_distances = current_time - event_times[p + 1 :]
        std::vector<double> target_distances;
        for(auto it = events_times.begin() + AR_ORDER_ + 1; it != events_times.end(); ++it){
            target_distances.push_back( current_time - *it);
        }
        // eta = weights_producer(current_time - uk)
        Eigen::VectorXd eta_ = weightsProducer.produce(target_distances);

        return PointProcessDataset((unsigned char) a.size() , AR_ORDER_, hasTheta0_, xn_, wn_, eta_, xt_, wt_);
    }
};


#endif //POINTPROCESS_POINTPROCESSDATASET_H
