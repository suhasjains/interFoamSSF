/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    interFoamSSF

Description
    Solver for 2 incompressible, isothermal immiscible fluids using a VOF
    (volume of fluid) phase-fraction based interface capturing approach.

    The momentum and other fluid properties are of the "mixture" and a single
    momentum equation is solved.

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "MULES.H"
#include "subCycle.H"
#include "interfaceProperties/interfaceProperties.H"
#include "twoPhaseMixture.H"
#include "interpolationTable.H"
#include "pimpleControl.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createMesh.H"

    pimpleControl pimple(mesh);

    #include "initContinuityErrs.H"
    #include "createFields.H"
    #include "readTimeControls.H"
    #include "correctPhi.H"
    #include "CourantNo.H"
    #include "setInitialDeltaT.H"

    // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

    Info<< "\nStarting time loop\n" << endl;

    while (runTime.run())
    {
        #include "readTimeControls.H"
        #include "CourantNo.H"
        #include "alphaCourantNo.H"
        #include "setDeltaT.H"

        runTime++;

        Info<< "Time = " << runTime.timeName() << nl << endl;

        twoPhaseProperties.correct();

        rho == alpha1*rho1 + (scalar(1) - alpha1)*rho2;

		//update interface location for t = t_n-1 + dt/2
        int cycle = 0;
        // --- Outer corrector loop
        scalar time = runTime.time().value();
        double timestep(runTime.deltaTValue());
        label tindex(runTime.timeIndex());

        // --- Pressure-velocity PIMPLE corrector loop
        for (pimple.start(); pimple.loop() || cycle<3; pimple++)
        {
			if (++cycle == 1)
            {
                Info << "Subcycle 1" << endl;

                Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;
                runTime.setDeltaT(timestep/2.0);

                Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;
                runTime.setTime(time - timestep/2.0, tindex);

                Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;
                #include "alphaEqn.H"
                continue;
            } else {
                Info << "Subcycle 2" << endl;

                Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;
                runTime.setDeltaT(timestep/2.0);
                runTime.setTime(time, tindex);

                Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;
                #include "alphaEqn.H"
                //need to accumulate rhoPhi
            }
            //Qwestshin - do we have to restore timeIndex too?
            //reset timestep and time
			Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;
            runTime.setTime(time, tindex);
            runTime.setDeltaT(timestep);

            Info << "Time, step: " << runTime.time().value() << ", " << runTime.deltaTValue() << endl;

			//surfaceScalarField fcf_old = interface.sigma()*fvc::snGrad(alpha1)*interface.Kf();
			surfaceScalarField fcf_old = interface.sigma()*fvc::snGrad(alpha1)*fvc::interpolate(interface.K());

            //update properties
            rho == alpha1*rho1 + (scalar(1) - alpha1)*rho2;
            //mu = alpha1*mu1 + (scalar(1) - alpha1)*mu2;

            interface.correct();

            //surface force
            scalar Cpc = 0.5;
            volScalarField alpha_pc = 1.0/(1.0-Cpc) * (min( max(alpha1,Cpc/2.0), (1.0-Cpc/2.0) ) - Cpc/2.0);
            surfaceScalarField deltasf = fvc::snGrad(alpha_pc);
            
            //surfaceScalarField fcf = interface.sigma()*interface.Kf()*deltasf;
            surfaceScalarField fcf = interface.sigma()*fvc::interpolate(interface.K())*deltasf;
            volVectorField fc = fvc::average(fcf*mesh.Sf()/mesh.magSf());
            fcf_old = fcf;
            
            // relax capillary force
            if (!pimple.finalIter()) fcf = 0.7 * fcf_old + 0.3 * fcf;

            // solve capillary pressure
            fvScalarMatrix pcEqn
            (
                fvm::laplacian(pc) == fvc::div(fc)
            );
            pcEqn.setReference(pRefCell, getRefCellValue(p, pRefCell));
            pcEqn.solve();

            #include "UEqn.H"
            // --- PISO loop
            for (int corr=0; corr<pimple.nCorr(); corr++)
            {
                #include "pEqn.H"
            }
			Info << "max(U): " << max(U) << endl;

        } // pimple loop

        runTime.write();

        Info<< "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
            << "  ClockTime = " << runTime.elapsedClockTime() << " s"
            << nl << endl;
    }

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //