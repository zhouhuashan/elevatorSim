/*
 * Copyright (c) 2012, Joseph Max DeLiso
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include "ElevatorSim.hpp"
#include "Elevator.hpp"
#include "ISimulationTerminal.hpp"
#include "Building.hpp"
#include "Floor.hpp"
#include "Logger.hpp"
#include "cTimeManager.hpp"

#include <boost/math/special_functions/round.hpp>
#include <boost/static_assert.hpp>
#include <vector>
#include <iostream>
#include <sstream>
#include <cassert>
#include <ctime>
#include <cmath>

namespace elevatorSim {

const int Elevator::DEFAULT_MAX_VEL = 120;
const int Elevator::DEFAULT_MAX_ACCEL = 4;
const int Elevator::DEFAULT_MAX_OCCUPANTS = 100;//12;

Elevator::Elevator(
   int _yVal,
   const int _numFloors,
   Floor** _floorInfo,
   const int _maxVel,
   const int _maxAccel,
   const int _maxOccupants) :
   floorInfo(_floorInfo),
   maxVel(_maxVel),
   maxAccel(_maxAccel),
   maxOccupants(_maxOccupants),
   numFloors(_numFloors),

   /* deceleration time from max speed to 0,
    * or acceleration time from 0 to max speed */
   accelTimeInterval(maxVel / maxAccel),

   /* distance required to stop when traveling
    * at maximum speed and then experiencing
    * negative maximum acceleration in the opposing
    * the direction of motion */
   stoppingDistance(
      maxVel * accelTimeInterval -
      maxAccel * (accelTimeInterval * accelTimeInterval ) / 2) {

         /* avoid truncation issues */
         assert(maxVel % maxAccel == 0);

         /* make sure that the elevator can achieve maximum speed both
          * while accelerating and slowing down between one floor */
         assert( 2 * stoppingDistance < Floor::YVALS_PER_FLOOR);

         yVal = _yVal;
         currentVel = 0;
         currentAccel = 0; 
         floorsSignaled = new bool[numFloors];

         /* FOR DEBUG */
         
         Person p(Location(0), Location(rand()%(numFloors-1)+1));
         occupants.push_back(p);
         scheduledFloors.push_back(p.getDestination().getYVal());
         
         if(isDebugBuild()) {
            std::stringstream dbgSS;
            dbgSS << "constructed elevator @" << this << std::endl;
            LOG_INFO( Logger::SUB_MEMORY, sstreamToBuffer( dbgSS ));
         }
}

void Elevator::scheduleAccelsToFloor( const int srcFloor, const int destfloor ) {
   assert(destfloor >= 0 && destfloor < numFloors && srcFloor == (yVal / Floor::YVALS_PER_FLOOR));

   /* height of the target floor in yVals */
   int targetFloorHeight = destfloor *  Floor::YVALS_PER_FLOOR;
   int thisFloorHeight = srcFloor *  Floor::YVALS_PER_FLOOR;

   /* the distance traveled at the maximum speed */
   int maxVelTimeInterval = boost::math::iround(
      ((float)(abs(yVal - targetFloorHeight) - 2 * stoppingDistance) / maxVel));
      
   /* retrieve current logic clock */
   const int currentTime =
      SimulationState::acquire().getTime();
      
   /* ensure that the total distance scheduled to be traveled is exactly
    * equal to the different in heights between current and distination */
   assert(maxVelTimeInterval * maxVel + 2 * stoppingDistance ==
      abs(targetFloorHeight - thisFloorHeight));
      
   /* print debug info */
   if(isDebugBuild()) {
      std::stringstream dbgSS;
      dbgSS << "with elevator @ " << this
         << " time t=" << currentTime
         << " a = " << currentAccel << " v = " << currentVel << " y = " << yVal
         << " and " << scheduledAccels.size() << " scheduled accels. " << std::endl;

      LOG_INFO( Logger::SUB_ELEVATOR_LOGIC, sstreamToBuffer( dbgSS ));
   }

   /* push them onto the back of the vector in reverse order */
   scheduledAccels.push_back(
      std::pair<int, int> ( currentTime + 2 * accelTimeInterval + maxVelTimeInterval, 0 ));

   scheduledAccels.push_back(
      std::pair<int, int> ( currentTime + accelTimeInterval + maxVelTimeInterval, 
      ( yVal < targetFloorHeight) ? ( -maxAccel ) : ( maxAccel )));

   scheduledAccels.push_back(
      std::pair<int, int> ( currentTime + accelTimeInterval, 0 ));

   scheduledAccels.push_back(
      std::pair<int, int> ( currentTime,
      ( yVal < targetFloorHeight) ? ( maxAccel ) : ( -maxAccel )));  

}

Elevator::~Elevator() {
   delete [] floorsSignaled;

   if(isDebugBuild()) {
      std::stringstream dbgSS;
      dbgSS << "destructing delevator @" << this << std::endl;
      LOG_INFO( Logger::SUB_MEMORY, sstreamToBuffer( dbgSS ));
   }
}

bool Elevator::canStopAtNextFloor() {
   /*
    * This function checks the acceleration and velocity and position and
    * returns true if the elevator can stop at the floor it's headed towards.
    * If the elevator is not accelerating, the function returns false.
    */

   if(currentAccel == 0) {
      return false;
   }

   /* compute next floor by truncating current and adding or subtracting
    * based on the current velocity */
   int nextFloor = int(yVal / Floor::YVALS_PER_FLOOR) 
      + ((currentVel > 0) ? (1) : (-1));

   int nextFloorHeight = nextFloor * Floor::YVALS_PER_FLOOR;

   /* compute distance to next floor */
   int nextFloorDistance = abs( nextFloorHeight - yVal );

   /* check if there's ample distance to stop */
   bool canStop = nextFloorDistance <= stoppingDistance;

   if(isDebugBuild()) {
      std::stringstream dbgSS;
      dbgSS << "with elevator @ " << this 
        << " and a = " << currentAccel << " v = " << currentVel << " y = " << yVal
        << std::endl << " SD= " << stoppingDistance << " NFD= " << nextFloorDistance
        << " can stop? " << canStop << std::endl;

      LOG_INFO( Logger::SUB_ELEVATOR_LOGIC, sstreamToBuffer( dbgSS ));
   }

   return canStop;
}

void Elevator::goToFloor(int floor) {
   scheduledFloors.push_back(floor);
}

void Elevator::init() {
   currentAccel = 0;
   currentVel = 0;

   /* TODO: use a safer container */
   for(int i = 0; i < numFloors; ++i) {
      floorsSignaled[i] = false;
   }

   occupants.clear();
   scheduledFloors.clear();
   scheduledAccels.clear();
}

void Elevator::render() {
   glCallList(cRenderObjs::OBJ_ELEVATOR);

   int num = getOccupantSize();
   if(num > 0)
      glCallList(cRenderObjs::OBJ_HUMAN);
}

void Elevator::update() {
   const int minElevHeight =
      SimulationState::acquire().getBuilding().getMinElevHeight();
   const int maxElevHeight =
      SimulationState::acquire().getBuilding().getMaxElevHeight();
   const int currentTime =
      SimulationState::acquire().getTime();

   /* ensure that height and velocity and acceleration are within legal ranges */
   assert( minElevHeight <= yVal && yVal <= maxElevHeight );
   assert( -maxVel <= currentVel && currentVel <= maxVel );
   assert(
      currentAccel == -maxAccel ||
      currentAccel == maxAccel ||
      currentAccel == 0 );

   /* are we on stopped on a floor with another floor scheduled? */
   if( currentVel == 0 && yVal % Floor::YVALS_PER_FLOOR == 0 && scheduledFloors.size() > 0 ) {
      const int thisFloor = (yVal / Floor::YVALS_PER_FLOOR);
      const int nextFloor = scheduledFloors.back();

      /* TODO: floor arrival processing */

      //take people out from elevator
      std::vector<Person>::iterator itr = occupants.begin();
      while(itr != occupants.end())
      {
         if(itr->getDestination().getYVal() == getCurrentFloor())  {
            itr = occupants.erase(itr);
         }

         else  itr++;
      }

      //Take people from floor

      scheduledFloors.pop_back();

      /* FOR DEBUG: schedule a new random dest */

      if( scheduledFloors.size() == 0 ) {
         //scheduledFloors.push_back( rand() % numFloors );

         /*int currFloor = getCurrentFloor();
         int farthest = currFloor;
         int goingUp = 2;	//0 - up	1 - down	2 - doesn't matter
         
         itr = occupants.begin();

         while(itr != occupants.end())
         {
            if(abs(itr->getDestination().getYVal() - currFloor) > abs(farthest - currFloor)) {
               farthest = itr->getDestination().getYVal();
            }

			itr++;
         }

         if(farthest - currFloor < 0)  {		goingUp = 1; }   //going down
         else if(farthest - currFloor > 0) {    goingUp = 0;  }   //going up
		 else									goingUp = 2;	//doesn't matter

         std::vector<Person> *floorOccupants = floorInfo[getCurrentFloor()]->getOccupants();
         itr = floorOccupants->begin();

         while(itr != floorOccupants->end() && getOccupantSize() != maxOccupants) {
            int dest = itr->getDestination().getYVal();

            if(goingUp == 1 && dest - currFloor < 0) {
               itr++;
               continue;
            }

            if(goingUp == 0 && dest - currFloor > 0)   {
               itr++;
               continue;
            }

            occupants.push_back(*itr);
            itr = floorOccupants->erase(itr);
         }

         int nextDest = currFloor;
         itr = occupants.begin();
         while(itr != occupants.end())
         {
            if(goingUp == 1 && itr->getDestination().getYVal() - currFloor < 0) {
				itr++; 
				continue;
			}
            if(goingUp == 0 && itr->getDestination().getYVal() - currFloor > 0) {
				itr++; 
				continue;
			}

            if(abs(itr->getDestination().getYVal() - currFloor) < abs(nextDest - currFloor)) {
               nextDest = itr->getDestination().getYVal();
            }

			itr++;
         }

		 if(nextDest == currFloor)	nextDest = rand() % numFloors;

         scheduledFloors.push_back(nextDest);*/

         std::vector<Person> *floorOccupants = floorInfo[getCurrentFloor()]->getOccupants();
         itr = floorOccupants->begin();

         while(itr != floorOccupants->end() && getOccupantSize() != maxOccupants) {
            occupants.push_back(*itr);
            itr = floorOccupants->erase(itr);
         }

         floorInfo[getCurrentFloor()]->recheckButtonPressed();

		  scheduledFloors.push_back(rand() % numFloors);

      }

      if( thisFloor != nextFloor ) {
         scheduleAccelsToFloor(thisFloor, nextFloor);
      }
   }

   /* is there a scheduled acceleration pending? */
   if( scheduledAccels.size() ) {
      std::pair<int, int> nextScheduledAccel = scheduledAccels.back();

      /* is the next scheduled acceleration now? */
      if( nextScheduledAccel.first == currentTime ) {

         /* remove it from the vector */
         scheduledAccels.pop_back();

         /* print debug info */
         if(isDebugBuild()) {
            std::stringstream dbgSS;
            dbgSS << "with elevator @ " << this 
               << " pulling scheduled accel off of queue at t = " << currentTime
               << ", a = " << currentAccel << ", v = " << currentVel << ", y = " << yVal
               << " and now " << scheduledAccels.size() 
               << " scheduled accels. NEW ACCEL: " << nextScheduledAccel.second << std::endl;

            LOG_INFO( Logger::SUB_ELEVATOR_LOGIC, sstreamToBuffer( dbgSS ));
         }

         /* and overwrite the current accel */
         currentAccel = nextScheduledAccel.second;
      }
   } 

   /* update current velocity */
   currentVel += currentAccel;
   currentVel = ( currentVel > maxVel ) ? ( maxVel ) : ( currentVel );
   currentVel = ( currentVel < -maxVel ) ? ( -maxVel ) : ( currentVel ); 

   /* update current position */
   yVal += currentVel;
   yVal = (yVal > maxElevHeight ) ? ( maxElevHeight ) : ( yVal );
   yVal = (yVal < minElevHeight ) ? ( minElevHeight ) : ( yVal );

   if(isDebugBuild()) {
      std::stringstream dbgSS;
      dbgSS << "elevator @" << this 
         << " yVal " << yVal << " v " << currentVel 
         << " a " << currentAccel << std::endl;
      LOG_INFO( Logger::SUB_ELEVATOR_LOGIC, sstreamToBuffer( dbgSS ));
   } 
 
   /* ensure that height and velocity and acceleration are within legal ranges */
   assert( minElevHeight <= yVal && yVal <= maxElevHeight );
   assert( -maxVel <= currentVel && currentVel <= maxVel );
   assert(
      currentAccel == -maxAccel ||
      currentAccel == maxAccel ||
      currentAccel == 0  );
}

/*void Elevator::pickupOccupants(Floor* floor)
{
   std::vector<Person> *floorOccupants = floor->getOccupants();

   // Take out people from elevator
   if(getOccupantSize() > 0)  {
      std::vector<Person>::iterator p;
      for(p = occupants.begin(); p != occupants.end(); p++) {
         if(p->destination.getYVal() == getCurrentFloor())   {
            occupants.erase(p);
         }
      }
   }

   // Bring people from floor
   std::for_each(
      floorOccupants->begin(),
      floorOccupants->end(),
      [] (Person p) {

   });
   
   //goToFloor(rand() % m_nFloor);
}*/

int Elevator::getCurrentFloor()   {
   return yVal / Floor::YVALS_PER_FLOOR;
}

} /* namespace elevatorSim */
