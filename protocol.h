#pragma once

#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <unistd.h>

using namespace std;

const size_t MAX_MSG = 32 << 20; // 32MB
const size_t MAX_ARGS = 200 * 1000;

const uint32_t RES_OK = 0;
const uint32_t RES_ERR = 1;
const uint32_t RES_NX = 2;

// out = "ab" , value = 3 -> out = "ab"[3]
void append_u32(string &out, uint32_t value)
{
    string bytes(4, '\0');
    memcpy(&bytes[0], &value, 4);
    out += bytes;
}

// data = [3][5] , pos = 0 , out -> out = 3 , pos = 4 , returns true (false if fewer than 4 bytes are left)
bool read_u32(const string &data, size_t &pos, uint32_t &out)
{
    if (pos + 4 > data.size())
        return false;
    memcpy(&out, &data[pos], 4);
    pos += 4;
    return true;
}

// data = "xxhello" , pos = 2, n = 5 , out -> out = "hello" , pos = 7 , return true/false.
bool read_str(const string &data, size_t &pos, size_t n, string &out)
{
    if (pos + n > data.size())
        return false;
    out = data.substr(pos, n);
    pos += n;
    return true;
}

// body = "hello" -> returns [5]hello
string make_packet(const string &body)
{
    string packet;
    append_u32(packet, body.size());
    packet += body;
    return packet;
}

// client uses it
//  cmd = {"set","name","sid"} -> [3][3]set[4]name[3]sid
string make_request_body(const vector<string> &cmd)
{
    string body;
    append_u32(body, cmd.size());
    for (const string &s : cmd)
    {
        append_u32(body, s.size());
        body += s;
    }
    return body;
}

// server uses it
//  body = [3][3]set[4]name[3]sid , cmd -> cmd = {"set","name","sid"}, return true/false
bool parse_request(const string &body, vector<string> &cmd)
{
    size_t pos = 0;
    uint32_t nstr = 0;
    if (!read_u32(body, pos, nstr))
        return false;
    if (nstr > MAX_ARGS)
        return false;

    while (cmd.size() < nstr)
    {
        uint32_t len = 0;
        if (!read_u32(body, pos, len))
            return false;
        string s;
        if (!read_str(body, pos, len, s))
            return false;
        cmd.push_back(s);
    }

    if (pos != body.size())
        return false;
    return true;
}

// server uses it
// status = RES_OK , data = "sid" -> [0]sid
string make_response_body(uint32_t status, const string &data)
{
    string body;
    append_u32(body, status);
    body += data;
    return body;
}

// client uses it
// body = [0]sid -> status = 0 , data = "sid" , return true/false.
bool parse_response(const string &body, uint32_t &status, string &data)
{
    size_t pos = 0;
    if (!read_u32(body, pos, status))
        return false;
    data = body.substr(pos);
    return true;
}