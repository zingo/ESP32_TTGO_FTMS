
#pragma once
#include <string>

class treadmill
{
    public:
    float max_speed;
    float min_speed;
    float max_incline; // incline/grade in percent(!)
    float min_incline;
    float speed_interval_min;
    float incline_interval_min;
    long  belt_distance; // mm ... actually circumfence of motor wheel!

    treadmill() {}

    std::string getName()
    {
        return "";
    }
};

class treadmillTaurus9_5 : public treadmill
{
    public:

    treadmillTaurus9_5()
    {
        max_speed   = 22.0;
        min_speed   =  0.5;
        max_incline = 11.0; // incline/grade in percent(!)
        min_incline =  0.0;
        speed_interval_min    = 0.1;
        incline_interval_min  = 1.0;
        belt_distance = 250; // mm ... actually circumfence of motor wheel!
    }

    std::string getName()
    {
        return "Taurus 9.5";
    }
};

class treadmillNorthtrack12_2_Si : public treadmill
{
    public:
    treadmillNorthtrack12_2_Si()
    {
        max_speed   = 20.0;
        min_speed   =  0.5;
        max_incline = 12.0; // incline/grade in percent(!)
        min_incline =  0.0;
        speed_interval_min    = 0.1;
        incline_interval_min  = 0.5;
        belt_distance = 153.3; // mm ... actually circumfence of motor wheel!
    }
    
    std::string getName()
    {
        return "Northtrack 12.2 Si";
    }
};