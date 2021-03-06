#include "Framework/CFSide.hh"
#include "Framework/MethodCommandProvider.hh"
#include "Framework/MeshData.hh"

#include "MathTools/MathFunctions.hh"

#include "SpectralFD/SpectralFDElementData.hh"

#include "SpectralFDNavierStokes/SpectralFDNavierStokes.hh"
#include "SpectralFDNavierStokes/TVBLimiterEulerSpectralFD.hh"

//////////////////////////////////////////////////////////////////////////////

using namespace std;
using namespace COOLFluiD::Common;
using namespace COOLFluiD::Framework;
using namespace COOLFluiD::MathTools;
using namespace COOLFluiD::Common;

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {

  namespace SpectralFD {

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::defineConfigOptions(Config::OptionList& options)
{
  options.addConfigOption< bool >("DensPresPosCheck","Check for density and pressure positivity or not.");
  options.addConfigOption< CFreal >("MinDensity","Minimum allowable value for density.");
  options.addConfigOption< CFreal >("MinPressure","Minimum allowable value for pressure.");
}

//////////////////////////////////////////////////////////////////////////////

TVBLimiterEulerSpectralFD::TVBLimiterEulerSpectralFD(const std::string& name) :
  TVBLimiterSpectralFD(name),
  m_nbrEqsMinusOne(),
  m_positivityCheck(),
  m_minDensity(),
  m_minPressure()
{
  addConfigOptionsTo(this);

  m_positivityCheck = false;
  setParameter( "DensPresPosCheck", &m_positivityCheck );

  m_minDensity = 1e-1;
  setParameter( "MinDensity", &m_minDensity );

  m_minPressure = 1e-1;
  setParameter( "MinPressure", &m_minPressure );
}

//////////////////////////////////////////////////////////////////////////////

TVBLimiterEulerSpectralFD::~TVBLimiterEulerSpectralFD()
{
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::configure ( Config::ConfigArgs& args )
{
  TVBLimiterSpectralFD::configure(args);
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::reconstructCellAveragedState()
{
  for (CFuint iEq = 0; iEq < m_nbrEqs; ++iEq)
  {
    reconstructCellAveragedVariable(iEq);
  }
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::reconstructCellAveragedVariable(const CFuint iEq)
{
  if (iEq == m_nbrEqsMinusOne)
  {
    CFreal pressure = 0.0;
    computePressFromConsVar(*(*m_cellStates)[0],pressure);
    m_cellAvgState[iEq] = (*m_cellAvgSolCoefs)[0]*pressure;
    for (CFuint iSol = 1; iSol < m_nbrSolPnts; ++iSol)
    {
      computePressFromConsVar(*(*m_cellStates)[iSol],pressure);
      m_cellAvgState[iEq] += (*m_cellAvgSolCoefs)[iSol]*pressure;
    }
  }
  else
  {
    TVBLimiterSpectralFD::reconstructCellAveragedVariable(iEq);
  }

  // density and pressure positivity check
  if (m_positivityCheck)
  {
    if (iEq == 0)
    {
      m_cellAvgState[iEq] = m_cellAvgState[iEq] > m_minDensity ? m_cellAvgState[iEq] : m_minDensity;
    }

    if (iEq == m_nbrEqsMinusOne)
    {
      m_cellAvgState[iEq] = m_cellAvgState[iEq] > m_minPressure ? m_cellAvgState[iEq] : m_minPressure;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::computeCellCenterDerivVariable(const CFuint iEq)
{
  if (iEq == m_nbrEqsMinusOne)
  {
    CFreal pressure = 0.0;
    computePressFromConsVar(*(*m_cellStates)[0],pressure);
    for (CFuint iDeriv = 0; iDeriv < m_dim; ++iDeriv)
    {
      m_cellCenterDerivVar[iDeriv] = (*m_cellCenterDerivCoefs)[iDeriv][0]*pressure;
    }
    for (CFuint iSol = 1; iSol < m_nbrSolPnts; ++iSol)
    {
      computePressFromConsVar(*(*m_cellStates)[iSol],pressure);
      for (CFuint iDeriv = 0; iDeriv < m_dim; ++iDeriv)
      {
        m_cellCenterDerivVar[iDeriv] += (*m_cellCenterDerivCoefs)[iDeriv][iSol]*pressure;
      }
    }
  }
  else
  {
    TVBLimiterSpectralFD::computeCellCenterDerivVariable(iEq);
  }
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::setLimitBooleans()
{
  // compute length scale factor
  const CFreal lengthScaleFactor = m_tvbLimitFactor*pow(m_cell->computeVolume(),m_lengthScaleExp);

  // reset m_applyLimiter
  m_applyLimiter = false;

  // check if limiting is necessary
  for (CFuint iEq = 0; iEq < m_nbrEqsMinusOne && !m_applyLimiter; ++iEq)
  {
    // compute upper and lower boundaries
    const CFreal dVar = lengthScaleFactor*(m_maxAvgStateAll[iEq] - m_minAvgStateAll[iEq]);
    const CFreal lowerBnd = m_minAvgState[iEq] - dVar;
    const CFreal upperBnd = m_maxAvgState[iEq] + dVar;
    for (CFuint iFlx = 0; iFlx < m_nbrFlxPnts && !m_applyLimiter; ++iFlx)
    {
      m_applyLimiter = (*m_solInFlxPnts[iFlx])[iEq] < lowerBnd ||
                       (*m_solInFlxPnts[iFlx])[iEq] > upperBnd;
    }
  }

  // check pressure
  if (!m_applyLimiter)
  {
    // compute upper and lower boundaries
    const CFreal dVar = lengthScaleFactor*(m_maxAvgStateAll[m_nbrEqsMinusOne] - m_minAvgStateAll[m_nbrEqsMinusOne]);
    const CFreal lowerBnd = m_minAvgState[m_nbrEqsMinusOne] - dVar;
    const CFreal upperBnd = m_maxAvgState[m_nbrEqsMinusOne] + dVar;
    CFreal pressure = 0.0;
    for (CFuint iFlx = 0; iFlx < m_nbrFlxPnts && !m_applyLimiter; ++iFlx)
    {
      computePressFromConsVar(*m_solInFlxPnts[iFlx],pressure);
      m_applyLimiter = pressure < lowerBnd ||
                       pressure > upperBnd;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::limitStates()
{
  for (CFuint iEq = 0; iEq < m_nbrEqsMinusOne; ++iEq)
  {
    // reconstruct the cell averaged variable and the derivatives in the cell center
    reconstructCellAveragedVariable(iEq);
    computeCellCenterDerivVariable (iEq);

    // limit the gradient to ensure that the limited solution is within the boundaries [UAvgMin,UAvgMax]
    // U = Uavg + dUdksi*ksi + dUdeta*eta + dUdzta*zta, with ksi, eta and zta in [-1,+1]
    // ==> Umax = Uavg + abs(dUdksi) + abs(dUdeta) + abs(dUdzta)
    // ==> Umin = Uavg - abs(dUdksi) - abs(dUdeta) - abs(dUdzta)
    CFreal dSolCellMax = std::abs(m_cellCenterDerivVar[KSI]);
    for (CFuint iCoor = 1; iCoor < m_dim; ++iCoor)
    {
      dSolCellMax += std::abs(m_cellCenterDerivVar[iCoor]);
    }

    if (dSolCellMax > MathTools::MathConsts::CFrealMin())
    {
      CFreal limitFact = 1.0;
      if (m_cellAvgState[iEq] + dSolCellMax > m_maxAvgState[iEq])
      {
        limitFact = (m_maxAvgState[iEq]-m_cellAvgState[iEq])/dSolCellMax;
      }
      if (m_cellAvgState[iEq] - dSolCellMax < m_minAvgState[iEq])
      {
        const CFreal limitFact2 = (m_cellAvgState[iEq]-m_minAvgState[iEq])/dSolCellMax;
        limitFact = limitFact2 < limitFact ? limitFact2 : limitFact;
      }
      m_cellCenterDerivVar *= limitFact;
    }

    // set the solution in the solution points
    for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
    {
      (*(*m_cellStates)[iSol])[iEq] = m_cellAvgState[iEq];
      for (CFuint iCoor = 0; iCoor < m_dim; ++iCoor)
      {
        (*(*m_cellStates)[iSol])[iEq] +=
            m_cellCenterDerivVar[iCoor]*(*m_solPntsLocalCoords)[iSol][iCoor];
      }
    }
  }

  // limit pressure
  // reconstruct the cell averaged variable and the derivatives in the cell center
  reconstructCellAveragedVariable(m_nbrEqsMinusOne);
  computeCellCenterDerivVariable (m_nbrEqsMinusOne);

  // check whether cell averaged pressure is within min/max limits prescribed
  // by current neighbouring cell averages and correct if necessary.
  // (The cell averaged pressure changes because of the limiting of the other variables)
  if (m_cellAvgState[m_nbrEqsMinusOne] > m_maxAvgState[m_nbrEqsMinusOne])
  {
    m_maxAvgState[m_nbrEqsMinusOne] = m_cellAvgState[m_nbrEqsMinusOne];

    /// @todo correct node neighbouring cell averages?
  }
  else if (m_cellAvgState[m_nbrEqsMinusOne] < m_minAvgState[m_nbrEqsMinusOne])
  {
    m_minAvgState[m_nbrEqsMinusOne] = m_cellAvgState[m_nbrEqsMinusOne];

    /// @todo correct node neighbouring cell averages?
  }

  // limit the gradient to ensure that the limited solution is within the boundaries [UAvgMin,UAvgMax]
  // U = Uavg + dUdksi*ksi + dUdeta*eta + dUdzta*zta, with ksi, eta and zta in [-1,+1]
  // ==> Umax = Uavg + abs(dUdksi) + abs(dUdeta) + abs(dUdzta)
  // ==> Umin = Uavg - abs(dUdksi) - abs(dUdeta) - abs(dUdzta)
  CFreal dSolCellMax = std::abs(m_cellCenterDerivVar[KSI]);
  for (CFuint iCoor = 1; iCoor < m_dim; ++iCoor)
  {
    dSolCellMax += std::abs(m_cellCenterDerivVar[iCoor]);
  }

  if (dSolCellMax > MathTools::MathConsts::CFrealMin())
  {
    CFreal limitFact = 1.0;
    if (m_cellAvgState[m_nbrEqsMinusOne] + dSolCellMax > m_maxAvgState[m_nbrEqsMinusOne])
    {
      limitFact = (m_maxAvgState[m_nbrEqsMinusOne]-m_cellAvgState[m_nbrEqsMinusOne])/dSolCellMax;
    }
    if (m_cellAvgState[m_nbrEqsMinusOne] - dSolCellMax < m_minAvgState[m_nbrEqsMinusOne])
    {
      const CFreal limitFact2 = (m_cellAvgState[m_nbrEqsMinusOne]-m_minAvgState[m_nbrEqsMinusOne])/dSolCellMax;
      limitFact = limitFact2 < limitFact ? limitFact2 : limitFact;
    }
    m_cellCenterDerivVar *= limitFact;
  }

  // set the solution in the solution points
  for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
  {
    CFreal pressure = m_cellAvgState[m_nbrEqsMinusOne];
    for (CFuint iCoor = 0; iCoor < m_dim; ++iCoor)
    {
      pressure +=
          m_cellCenterDerivVar[iCoor]*(*m_solPntsLocalCoords)[iSol][iCoor];
    }
    if (pressure < 0.0)
    {
      CFLog(INFO,"Pressure at solution point " << StringOps::to_str(iSol) << ": "
                                               << StringOps::to_str(pressure) << "\n");
    }
    computeRhoEFromPressAndOtherConsVar(*(*m_cellStates)[iSol],pressure);
  }

/*  // check density and pressure positivity
  {
    // recompute the solutions in the flux points
    m_statesReconstr->reconstructStates(*m_cellStates,m_solInFlxPnts,
                                        *m_flxPntsRecCoefs,*m_allFlxPntIdxs,
                                        *m_flxPntMatrixIdxForReconstruction,
                                        *m_solPntIdxsForReconstruction);

    for (CFuint iFlx = 0; iFlx < m_nbrFlxPnts; ++iFlx)
    {
      // check density
      if ((*m_solInFlxPnts[iFlx])[0] < 0.0)
      {
        CFLog(INFO,"Density at flux point " << StringOps::to_str(iFlx) << ": "
                << StringOps::to_str((*m_solInFlxPnts[iFlx])[0]) << "\n");
      }

      // check pressure
      CFreal pressure = 0.0;
      computePressFromConsVar(*m_solInFlxPnts[iFlx],pressure);
      if (pressure < 0.0)
      {
        CFLog(INFO,"Pressure at flux point " << StringOps::to_str(iFlx) << ": "
                << StringOps::to_str(pressure) << "\n");
      }
    }
  }*/
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::setup()
{
  CFAUTOTRACE;

  TVBLimiterSpectralFD::setup();

  // number of equations minus one
  m_nbrEqsMinusOne = m_nbrEqs - 1;
}

//////////////////////////////////////////////////////////////////////////////

void TVBLimiterEulerSpectralFD::unsetup()
{
  CFAUTOTRACE;

  TVBLimiterSpectralFD::unsetup();
}

//////////////////////////////////////////////////////////////////////////////

  } // namespace SpectralFD

} // namespace COOLFluiD
