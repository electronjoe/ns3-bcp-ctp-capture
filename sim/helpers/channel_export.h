#pragma once
#include "ns3/propagation-module.h"
#include "ns3/mobility-model.h"

namespace calib {
double DbmToW(double dbm);
double ThermalNoiseDbm(double bandwidthHz, double noiseFigureDb);
double CalcMeanRxDbm(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                     double txPowerDbm,
                     ns3::Ptr<ns3::MobilityModel> tx,
                     ns3::Ptr<ns3::MobilityModel> rx);
double CalcPairSinrDb(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                      double txPowerDbm,
                      ns3::Ptr<ns3::MobilityModel> desiredTx,
                      ns3::Ptr<ns3::MobilityModel> rx,
                      ns3::Ptr<ns3::MobilityModel> interferer,
                      double bandwidthHz, double noiseFigureDb);
}
