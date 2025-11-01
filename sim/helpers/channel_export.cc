#include "channel_export.h"
#include <cmath>

namespace calib {
double DbmToW(double dbm){ return std::pow(10.0, (dbm - 30.0)/10.0); }
static double WToDb(double w){ return 10.0*std::log10(w); }
double ThermalNoiseDbm(double B, double NF){ return -174.0 + 10.0*std::log10(B) + NF; }
double CalcMeanRxDbm(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                     double txPowerDbm,
                     ns3::Ptr<ns3::MobilityModel> tx,
                     ns3::Ptr<ns3::MobilityModel> rx) {
  return loss->CalcRxPower(txPowerDbm, tx, rx);
}
double CalcPairSinrDb(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                      double txPowerDbm,
                      ns3::Ptr<ns3::MobilityModel> desiredTx,
                      ns3::Ptr<ns3::MobilityModel> rx,
                      ns3::Ptr<ns3::MobilityModel> interferer,
                      double B, double NF) {
  double sDbm = loss->CalcRxPower(txPowerDbm, desiredTx, rx);
  double iDbm = loss->CalcRxPower(txPowerDbm, interferer, rx);
  double nDbm = ThermalNoiseDbm(B, NF);
  double sW = DbmToW(sDbm), iW = DbmToW(iDbm), nW = DbmToW(nDbm);
  return WToDb(sW / (iW + nW));
}
}
