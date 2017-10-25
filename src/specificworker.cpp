/*
 *    Copyright (C) 2017 by YOUR NAME HERE
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include "specificworker.h"

/**
* \brief Default constructor
*/
SpecificWorker::SpecificWorker(MapPrx& mprx) : GenericWorker(mprx)
{

}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
	
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
  inner = new InnerModel("/home/robocomp/robocomp/files/innermodel/simpleworld.xml");
  timer.start(Period);
  return true;
}

void SpecificWorker::compute(){
  try
  {
      RoboCompLaser::TLaserData ldata = laser_proxy->getLaserData();
      //std::sort(ldata.begin(), ldata.end(), [](RoboCompLaser::TData a, RoboCompLaser::TData b){ return a.dist < b.dist; }) ;  //sort laser data from small to large distances using a lambda function.
      switch(robotState) {
	case State::IDLE: 
	  idleState();
	  break;
	case State::GOTO:	  
	  gotoState(ldata);
	  break;
	case State::TURN:
	  turnState(ldata);
	  break;
	case State::AVOID: 
	  avoidState(ldata);
	  break;
	case State::END: 
	  endState();
	  break;
      }
  }
  catch(const Ice::Exception &ex)
  {
      std::cout << ex << std::endl;
  }
}
	  
void SpecificWorker::idleState(){ 
  if (!target.isEmpty())
    robotState = State::GOTO;
  differentialrobot_proxy->setSpeedBase(0, 0);
  
}
void SpecificWorker::gotoState(RoboCompLaser::TLaserData ldata) {
  std::cout << "GOTO STATE!" << endl;  
  if(obstacle(ldata)){ /*Obstacle detected, left and right side*/
   differentialrobot_proxy->setSpeedBase(0, 0);
   robotState = State::TURN;
   decideTurnDirection(ldata);
   return;
  }

  //All variables are needed to calculate distance
  RoboCompDifferentialRobot::TBaseState state;
  differentialrobot_proxy->getBaseState(state);
  inner->updateTransformValues("base", state.x, 0, state.z, 0, state.alpha, 0); //Transforms the robot's vector to the world's point of view. (Or vice versa, don't remember).	    
  std::pair<float, float> t = target.getTarget();
  QVec tR = inner->transform("base", QVec::vec3(t.first, 0, t.second), "world"); //Vector's source is robot's location, vector's end is the mouse pick
  float d = tR.norm2(); //Gets the distance, that equals the vector's module
  //If no exit conditions
  if(d < MINDISTANCE){
    robotState = State::IDLE;
    target.setEmpty();
    std::cout << "The bomb has been planted." <<endl;
    return;
  }else{
    /*We do continue on the GOTO state*/
    float adv;
    float rot = atan2(tR.x(), tR.z());
    if (rot > MAXROT) rot = MAXROT;
    adv = MAXVEL * getSigmoid(d) * getGauss(rot, 0.3, 0.5);
    //printState(d, adv, rot);
    differentialrobot_proxy->setSpeedBase(adv, rot);
  }
}

void SpecificWorker::turnState (RoboCompLaser::TLaserData ldata) {
  std::cout << "TURN STATE!" << endl;  
  if(endTurnState(ldata)){ /*Obstacle sorting, checks if there is still an obstacle*/
    differentialrobot_proxy->setSpeedBase(0, 0);
    robotState = State::AVOID;
    return;
  }
  if(turnDirection == Turn::LEFT){
    differentialrobot_proxy->setSpeedBase(0, -1);
    return;}
  differentialrobot_proxy->setSpeedBase(0, 1); 
}

void SpecificWorker::avoidState (RoboCompLaser::TLaserData ldata){
  std::cout << "AVOID STATE!" << endl;  
//   if(targetAtSight(ldata) || cutVector(ldata)){
//     robotState = State::GOTO;
//     return;
//   }
  if(!checkIfStillObstacle(ldata)){
    if(turnDirection == Turn::RIGHT){
      differentialrobot_proxy->setSpeedBase(200, -1);
      turnDirection = Turn::LEFT;
      return;
    }
    differentialrobot_proxy->setSpeedBase(200, 1);
    turnDirection = Turn::RIGHT;
    return;
  }
  differentialrobot_proxy->setSpeedBase(200, 0);
}

void SpecificWorker::endState(){
  std::cout << "END STATE!" << endl;
  robotState = State::IDLE;
}

bool SpecificWorker::targetAtSight(RoboCompLaser::TLaserData ldata)
{
   QPolygonF polygon;
   QVec r = inner->transform("world", "base");
   polygon << QPointF(r.x(), r.z());
   for (int i = 0; i < 100; i++ ){
     QVec lr = inner->laserTo("world", "laser", ldata[i].dist, ldata[i].angle);
     polygon << QPointF(lr.x(), lr.z());
   }
   polygon << QPointF(r.x(), r.z());
   QPointF p(target.getTarget().first, target.getTarget().second);   
   return  polygon.containsPoint( p , Qt::WindingFill );
}

bool SpecificWorker::cutVector ( RoboCompLaser::TLaserData ldata ) { 
  return false;
}

bool SpecificWorker::obstacle(RoboCompLaser::TLaserData ldata){
  for(int i=leftAngle; i<= rightAngle; i++){
    if(ldata[i].dist < threshold)
      return true;
  }
  
  return false;
}

/**Checks it there is an obstacle after turning, taking into account the direction of the last turn **/
bool SpecificWorker::endTurnState(RoboCompLaser::TLaserData ldata){ 
  int start = middleAngle;
  int end = rightAngle;
  if(turnDirection == Turn::RIGHT){  
    start = leftAngle;
    end = middleAngle; 
  }
  for (int i = start; i<= end; i++){
    if(ldata[i].dist < threshold)
      return false;
  }
  return true;
}

/**Checks it there is an obstacle after turning, taking into account the direction of the last turn **/
bool SpecificWorker::checkIfStillObstacle(RoboCompLaser::TLaserData ldata){ 
  if((turnDirection == Turn::RIGHT && ldata[10].dist > threshold) || (turnDirection == Turn::LEFT && ldata[90].dist > threshold))
    return false;
  return true;
}



/*Devides the better turn, checking where there is "more" obstacle, on the left or on the right (Preference = left)*/
void SpecificWorker::decideTurnDirection(RoboCompLaser::TLaserData ldata)
{
  if(ldata[rightAngle].dist < threshold){/*Obstacle on the right (or in the right and in the left) (looking straight at it)*/
    turnDirection = Turn::LEFT;
    return;}
  /*If the obstacle is detected by the left part of the laser, we have to turn right*/
  turnDirection = Turn::RIGHT;
}

void SpecificWorker::printState(float d, float adv, float rot){
  std::cout << "-------------------------"<< endl;
  std::cout << "Sigmoid - " << getSigmoid(d) << endl;
  std::cout << "Gauss - " << getGauss(rot, 0.3, 0.5) << endl;
  std::cout << "Vel is - " << adv << endl;
  std::cout << "Rotation is - " << rot << endl;
  std::cout << "Distance is - " << d << endl;
  std::cout << "-------------------------"<< endl;
}

void SpecificWorker::printLaser(RoboCompLaser::TLaserData ldata, int start, int end){
  for(int i=start; i<=end; i++)
    std::cout << "Laser position " << i << " distance -> " << ldata[i].dist << endl;
}


float SpecificWorker::getGauss(float Vr, float Vx, float h){
  float lambda = -pow(Vx, 2)/log(h);
  return pow(EulerConstant, (-pow(Vr, 2)/lambda));
}

float SpecificWorker::getSigmoid(float distance){
  float x = distance / 100;  //In sigmoid function, the changes on the curve are around 1-2-3 in the X
  return x / (1 + abs(x)); //"fast" sigmoid function
  
}

void SpecificWorker::setPick(const RoboCompRCISMousePicker::Pick& pick)
{
  target.setTarget(pick.x, pick.z);
  std::cout << "Location x -> " << pick.x << " was chosen." << endl;
  std::cout << "Location z -> " << pick.z << " was chosen." << endl; 
}




/*RoboCompLaser::TLaserData ldata = laser_proxy->getLaserData();  //read laser data 
        std::sort(ldata.begin(), ldata.end(), [](RoboCompLaser::TData a, RoboCompLaser::TData b){ return a.dist < b.dist; }) ;  //sort laser data from small to large distances using a lambda function.
	differentialrobot_proxy->setSpeedBase(400, 0);
	if(ldata[20].dist < threshold)
	{
		//std::cout << ldata.front().dist << std::endl;
 		//differentialrobot_proxy->setSpeedBase(10, rot);
		differentialrobot_proxy->setSpeedBase(0, 1);
		usleep(rand()%(1500000-100000 + 1) + 100000);  //random wait between 1.5s and 0.1sec
	}

	try
	{
		camera_proxy->getYImage(0,img, cState, bState);
		memcpy(image_gray.data, &img[0], m_width*m_height*sizeof(uchar));
		searchTags(image_gray);
	}
	catch(const Ice::Exception &e)
	{
		std::cout << "Error reading from Camera" << e << std::endl;
	}
*/
