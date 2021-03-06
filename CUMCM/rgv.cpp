#include "rgv.h"


int rgv::get_ripe_count() const
{
    return _ripe;
}

rgv::state rgv::get_state() const
{
    return _state;
}

void rgv::update()
{
    switch (_state)
    {
    case rgv::MOVEING:
        if (*_pclock >= _stop)
        {
            _state = WAITING;
        }
        break;
    case rgv::WAITING:
        break;
    case rgv::LOADING:
        if (*_pclock >= _stop)
        {
            _state = WAITING;
            _material = material(); //set _material null
        }
        break;
    case rgv::UNLOADING:
        if (*_pclock >= _stop)
        {
            if (_material.get_state() == material::RIPE)
            {
                _state = WASHING;
                _stop = *_pclock + RGV_WASH_TIME;
            }
            else
            {
                _state = WAITING;
            }
        }
        break;
    case rgv::WASHING:
        if (*_pclock >= _stop)
        {
            ++_ripe;
            _state = WAITING;
            _material = material(); //set _material null
        }
        break;
    default:
        break;
    }
    //according to msg, decide next state here
    //
    //
    //
    //
    //
    //       coding here
    //
    //
    //
    //
    //
    if (_material.is_init() && _pmsg->size() == 0)
    {
        std::cout << "cannot find cnc time:" << *_pclock << std::endl;
    }
    if (_state == WAITING && _pmsg->size() > 0)
    {
        std::vector<msg_dst_pair> msg_with_dst;
        msg_with_dst.reserve(_pmsg->size());
        const message *pnext_demand = nullptr;
        for (const auto &one : *_pmsg)
        {
            msg_with_dst.push_back(std::make_pair(abs(get_pos(one.cnc_num) - _pos), one));
        }
        //sort by priority: demand_type(perfer loading) > distance > number of cnc(perfer odd number)
        std::sort(msg_with_dst.begin(), msg_with_dst.end(), std::bind(rgv::compare, std::placeholders::_1, std::placeholders::_2, _pcnc_array));
#ifdef MULTIPLE
        if (_material.is_init())    //holding a HALF material
        {
            pnext_demand;
            auto tmp = (std::find_if(msg_with_dst.cbegin(), msg_with_dst.cend(),
                [](const msg_dst_pair &a)->bool {return a.second.cnc_type == 2 && a.second.cnc_demand == message::WAIT_LOADING; }));
            if (tmp != msg_with_dst.cend())
            {
                pnext_demand = &tmp->second;
            }
            else
            {
                std::cout << "cannot find cnc time:" << *_pclock << std::endl;
            }
        }
        else
        {
            for (const auto &one : msg_with_dst)
            {
                if (one.second.cnc_type == 1 && one.second.cnc_demand == message::WAIT_UNLOADING)   //unload HALF
                {
                    auto tmp = std::find_if(msg_with_dst.cbegin(),
                        msg_with_dst.cend(), [](const msg_dst_pair &a)->bool {return a.second.cnc_type == 2 && a.second.cnc_demand == message::WAIT_LOADING; });
                    if (tmp != msg_with_dst.cend()) //has a free cnc for process 2
                    {
                        pnext_demand = &one.second;
                        break;
                    }
                }
                else
                {
                    if (one.second.cnc_demand == message::WAIT_LOADING)  //check material type before loading
                    {
                        if (one.second.cnc_type == 1)
                        {
                            pnext_demand = &one.second;
                            break;
                        }
                    }
                    else
                    {
                        pnext_demand = &one.second;
                        break;
                    }
                }
            }
        }
#else
        pnext_demand = &msg_with_dst.front().second;
#endif // MULTIPLE
        if (pnext_demand == nullptr)
            return;
        if (get_distance(*pnext_demand) == 0)
        {
            switch (pnext_demand->cnc_demand)
            {
            case message::WAIT_LOADING:
#ifdef MULTIPLE
                if (!_material.is_init())
                    _material = material(false);
#else
                _material = material(true);
#endif // MULTIPLE
                _pcnc_array[pnext_demand->cnc_num - 1].load(load(pnext_demand->cnc_num));
                _pmsg->erase(std::find(_pmsg->begin(), _pmsg->end(), *pnext_demand));
                break;
            case message::WAIT_UNLOADING:
                unload(_pcnc_array[pnext_demand->cnc_num - 1].unload(), pnext_demand->cnc_num);
                _pmsg->erase(std::find(_pmsg->begin(), _pmsg->end(), *pnext_demand));
                break;
            default:
                break;
            }
        }
        else if (get_distance(*pnext_demand) > 0)
        {
            move(get_pos(pnext_demand->cnc_num));
        }
    }
}

material rgv::load(int cnc_num)
{
    _state = LOADING;
    _stop = *_pclock + CNC_EVENT_TIME[cnc_num % 2 == 1 ? ODD_LOAD : EVEN_LOAD];
    _all_log[_material.get_id()].cnc_num = cnc_num;
#ifdef MULTIPLE
    if (_material.get_state() == material::RAW)
    {
        _all_log[_material.get_id()].load_raw_start = *_pclock;
    }
    else if (_material.get_state() == material::HALF)
    {
        _all_log[_material.get_id()].load_half_start = *_pclock;
    }
#else
    _all_log[_material.get_id()].load_start = *_pclock;
#endif // MULTIPLE

    return _material;
}

void rgv::unload(material m, int cnc_num)
{
    _state = UNLOADING;
    _material = m;
#ifdef MULTIPLE
    if (_material.get_state() == material::HALF)
    {
        _all_log[_material.get_id()].unload_half_start = *_pclock;
    }
    else if (_material.get_state() == material::RIPE)
    {
        _all_log[_material.get_id()].unload_pipe_start = *_pclock;
    }
#else
    _all_log[_material.get_id()].unload_start = *_pclock;
#endif // MULTIPLE
    _stop = *_pclock + CNC_EVENT_TIME[cnc_num % 2 == 1 ? ODD_UNLOAD : EVEN_UNLOAD];
}

void rgv::move(int pos)
{
    int dst = abs(pos - _pos);
    if (dst == 0)
    {
        _state = WAITING;
    }
    else if (dst >= 1 && dst <= 3)
    {
        _stop = *_pclock + RGV_MOVE_TIME[dst];
        _state = MOVEING;
    }
    _pos = pos;
}

void rgv::output_log(std::ostream & os) const
{
    using std::endl;
    os << "material_id" << ','
        << "cnc_num" << ',';
#ifdef MULTIPLE
    os << "load_raw_start" << ','
        << "unload_half_start" << ','
        << "load_half_start" << ','
        << "unload_pipe_start" << endl;
#else
    os << "load_start_time" << ','
        << "unload_start_time" << endl;
#endif // MULTIPLE

    for (const auto& one_log : _all_log)
    {
        os << one_log.first << ','
            << one_log.second.cnc_num << ',';
#ifdef MULTIPLE
        os << one_log.second.load_raw_start << ','
            << one_log.second.unload_half_start << ','
            << one_log.second.load_half_start << ','
            << one_log.second.unload_pipe_start << endl;
#else
        os << one_log.second.load_start << ','
            << one_log.second.unload_start << endl;
#endif // MULTIPLE
    }
}

rgv::rgv(time * pclock, std::vector<message> *pmsg, cnc *pcnc) :
    _pclock(pclock), _pmsg(pmsg), _pcnc_array(pcnc)
{
}

rgv::~rgv()
{
}

bool rgv::compare(const msg_dst_pair & a, const msg_dst_pair & b,const cnc * const pcnc_array)
{
#ifdef MULTIPLE
    //priority: demand_type(perfer unloading) > distance > number of cnc(perfer odd number)
    //unloading half material should be judged specially
    //return a.second.cnc_demand > b.second.cnc_demand    //demand_type(perfer unloading)
    //    || (a.second.cnc_demand == b.second.cnc_demand && a.first < b.first)    //distance
    //    || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num < b.second.cnc_num)    //number
    //    || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num == b.second.cnc_num && a.second.cnc_type > b.second.cnc_type);

    //priority: distance > demand_type(perfer unloading) > number of cnc(perfer odd number)
    //unloading half material should be judged specially
    //return a.first < b.first    //distance
    //    || (a.first == b.first && a.second.cnc_demand > b.second.cnc_demand)    //demand_type(perfer unloading)
    //    || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num < b.second.cnc_num)    //number
    //    || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num == b.second.cnc_num && a.second.cnc_type > b.second.cnc_type);

#ifdef BREAK_DOWN
    int bad[2] = { 0,0 };
    for (size_t i = 0; i < CNC_NUM; i++)
    {
        if (pcnc_array[i].get_state() == cnc::BROKEN)
            ++bad[pcnc_array[i].get_type() - 1];
    }
    if (bad[0] > bad[1])    //perfer loading RAW and unloading HALF
    {
        //priority: distance > demand_type(perfer loading) > number of cnc(perfer odd number)
        //unloading half material should be judged specially
        return a.first < b.first    //distance
            || (a.first == b.first && a.second.cnc_demand < b.second.cnc_demand)    //demand_type(perfer loading)
            || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num < b.second.cnc_num)    //number
            || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num == b.second.cnc_num && a.second.cnc_type > b.second.cnc_type);
    }
    else if (bad[0] == bad[1])  //original
    {
        //priority: distance > demand_type(perfer loading) > number of cnc(perfer odd number)
        //unloading half material should be judged specially
        return a.first < b.first    //distance
            || (a.first == b.first && a.second.cnc_demand < b.second.cnc_demand)    //demand_type(perfer loading)
            || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num < b.second.cnc_num)    //number
            || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num == b.second.cnc_num && a.second.cnc_type > b.second.cnc_type);
    }
    else    //perfer unloading RIPE
    {
        //priority: distance > demand_type(perfer unloading) > cnc_type (prefer unloading RIPE)> number of cnc(perfer odd number)
        //unloading half material should be judged specially
        return a.first < b.first    //distance
            || (a.first == b.first && a.second.cnc_demand > b.second.cnc_demand)    //demand_type(perfer unloading)
            || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_type > b.second.cnc_type )    //cnc_type
            || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_type == b.second.cnc_type && a.second.cnc_num < b.second.cnc_num);
    }
#else
    //priority: distance > demand_type(perfer loading) > number of cnc(perfer odd number)
    //unloading half material should be judged specially
    return a.first < b.first    //distance
        || (a.first == b.first && a.second.cnc_demand < b.second.cnc_demand)    //demand_type(perfer loading)
        || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num < b.second.cnc_num)    //number
        || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num == b.second.cnc_num && a.second.cnc_type > b.second.cnc_type);
#endif
#else
    //priority: demand_type(perfer loading) > distance > number of cnc(perfer odd number)
    return a.second.cnc_demand < b.second.cnc_demand
        || (a.second.cnc_demand == b.second.cnc_demand && a.first < b.first)
        || (a.second.cnc_demand == b.second.cnc_demand && a.first == b.first && a.second.cnc_num < b.second.cnc_num);

    //priority:  distance > number of cnc(perfer odd number)
    /*return (a.first < b.first)
        || (a.first == b.first && a.second.cnc_num < b.second.cnc_num);*/
#endif // MULTIPLE

}

int rgv::get_distance(const message &msg) const
{
    return abs(get_pos(msg.cnc_num) - _pos);
}
