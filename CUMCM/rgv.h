#ifndef RGV_H
#define RGV_H

#include<cmath>
#include<vector>
#include<algorithm>
#include"time.h"
#include"material.h"
#include"macro.h"
#include"message.h"
#include"cnc.h"
#include<map>
#include<functional>

class rgv
{
public:
    enum state
    {
        MOVEING,
        WAITING,
        LOADING,
        UNLOADING,
        WASHING
    };

    int get_ripe_count() const;
    state get_state() const;
    void update();
    //assume that RGV is located in the correct position
    material load(int cnc_num);
    void unload(material m, int cnc_num);
    void move(int pos);
    void output_log(std::ostream &os) const;

    rgv(time *pclock, std::vector<message> *pmsg, cnc *pcnc);
    ~rgv();

private:
    using msg_dst_pair = std::pair<int, message>;

    struct log
    {
        int cnc_num = 0;
#ifdef MULTIPLE
        time load_raw_start = 0;
        time unload_half_start = 0;
        time load_half_start = 0;
        time unload_pipe_start = 0;
#else
        time load_start = 0;
        time unload_start = 0;
#endif // MULTIPLE

    };

    static bool compare(const msg_dst_pair &a, const msg_dst_pair &b, const cnc * const pcnc_array);
    int get_distance(const message &msg) const;

    state _state = WAITING;
    //      CNC2#  CNC4#  CNC6#  CNC8#
    //_pos    0      1      2      3
    //      CNC1#  CNC3#  CNC5#  CNC7#
    int _pos = 0;
    material _material;
    time _stop;
    time *_pclock;
    std::vector<message> *_pmsg = nullptr;
    int _ripe = 0;
    std::map<int, log> _all_log;
    cnc *_pcnc_array = nullptr;
};

#endif // !RGV_H