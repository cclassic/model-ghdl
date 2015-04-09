#include <iostream>
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <fstream>
#include <string.h>

// #define DEBUG_EN DEBUG_EN

using namespace std;

// Source: vsim -gui design.top(RTL)
// Target: ghdl -m --warn-no-vital-generic --workdir=simu --work=work testb_file
//         ./testb_file --stop-time=500ns --vcdgz=testb_file.vcdgz
//         gunzip --stdout testb_file.vcdgz | gtkwave --vcd

int run(string args);

int run(string args) {
    FILE *proc;
    char buf[512];
    vector < string > result;

    proc = popen(args.c_str(), "r");

    if (proc == NULL) {
        cerr << "Error: Could not invoke GHDL/GtkWave." << endl;
        return 1;
    }

    while(fgets(buf, sizeof(buf), proc)!=NULL){
        cout << buf;
    }

    return pclose(proc);
}

string getSimulationTime() { // Very crude but works (for a proof-of-concept anyway^^)
    FILE *proc;
    char buf[512];
    vector < string > result;
    string defaultValue = "100ns";

    ifstream myfile;
    char fileTemp[128];
    myfile.open ("/tmp/ghdl-simtime");
    myfile.getline(fileTemp, sizeof(fileTemp));
    myfile.close();
    if (string(fileTemp) != "")
        defaultValue = string(fileTemp);

    string temp = "zenity --entry --text \"Enter the duration:\" --title \"Simulation time\" --entry-text=\"" + defaultValue + "\"";
    proc = popen(temp.c_str(), "r");

    if (proc == NULL) {
        cerr << "Error: Could not invoke zenity." << endl;
        return "";
    }

    while(fgets(buf, sizeof(buf), proc)!=NULL){
        cout << buf;
    }


    defaultValue.clear();
    char *ptr = buf;
    while (*ptr != '\0' && *ptr != '\n') {
        defaultValue.append(" ");
        defaultValue[defaultValue.length()-1] = *ptr;
        ptr++;
    }

    if (pclose(proc)) {
        defaultValue = "";
    }
    else {
        ofstream myfile;
        myfile.open ("/tmp/ghdl-simtime");
        myfile << defaultValue;
        myfile.flush();
        myfile.close();
    }

    return defaultValue;
}

#define ISOPT(cmd) (string(argv[i]) == cmd)
#define GETOPT(cmd) (string(argv[i]) == cmd) && (++i < argc)

int main(int argc, char **argv) {
    string top = "";
    string work = "";
    string simtime = "";
    string ghdlargs = "";
    string wvargs = "";
    string gtkargs = "";
    int i;
    char tempdir[256] = ""; // Compile dir

    ifstream myfile;
    myfile.open ("/tmp/ghdl-workdir");
    myfile.getline(tempdir, sizeof(tempdir));
    myfile.close();

    for (i=1; i < argc; ++i) {
        if (ISOPT("-gui")) {

        }
        else if (GETOPT("-compiledir")) {
            strcpy(tempdir, argv[i]);
        }
        else if (GETOPT("-ghdl")) {
            ghdlargs = argv[i];
        }
        else if (GETOPT("-rargs")) {
            wvargs = argv[i];
        }
        else if (GETOPT("-gtkwave")) {
            gtkargs = argv[i];
        }
        else {
            if (argv[i][0] == '-') {
                cerr << "INFO: Unknown command line opt: " << argv[i] << endl;
            }
            else {
                top.append(argv[i]);
                top.append(" ");
            }
        }
    }


    // -gui work.toplevel(RTL)
    string temp = "";
    for (unsigned int i=0; i < top.length(); ++i) {
        if (top.at(i) == '.') {
            work = temp;
            temp = "";
        }
        else if (top.at(i) == '(') {
            top = temp;
            temp = "";
            break;
        }
        else {
            temp.append(" ");
            temp[temp.length()-1] = top.at(i);
        }
    }

#ifdef DEBUG_EN
    cout << "\n\nVSIM CALL PARSED:" << endl;
    cout << "\twork=" << work << endl;
    cout << "\ttop=" << top<< endl;
    cout << "\ttempdir=" << tempdir << endl;
#endif

    if (work == "" || top == "" || string(tempdir) == "") {
        cerr << "Error: Incomplete/Unsupported vsim call." << endl;
        return 2;
    }


    //bool ex = false;
    //while (!ex) {
        cout << "Compiling..." << endl;
        string cargs = "cd " + string(tempdir) + "; ghdl -m --warn-no-vital-generic " + ghdlargs + " -fexplicit --workdir=" + string(tempdir) + " --work=" + work + " " + top;
#ifdef DEBUG_EN
    cout  << "RUN: " << cargs << endl;
#endif
        if (run(cargs)) {
            cerr << "Error: Compilation failed." << endl;
            run("zenity --error --text \"Compilation failed.\"");
        }
        else {
            cout << "done." << endl;
            cargs = "";
            string st = getSimulationTime();
            if (st != "") {
                cout << "Simulating..." << endl;
#ifdef DEBUG_EN
    cout  << "RUN: " << "cd " + string(tempdir) + "; ./" + top + " " + wvargs + " --stop-time=" + st + " --wave=" + top + ".ghw" << endl;
#endif
                if (run("cd " + string(tempdir) + "; ./" + top + " " + wvargs + " --stop-time=" + st + " --wave=" + top + ".ghw")) {
                    cerr << "Error: Simulation failed." << endl;
                }
                else {
                    cout << "==> All done!" << endl;
                    string wv = "gtkwave " + string(tempdir) + "/" + top + ".ghw " + gtkargs + " &";
                    if (run("pidof gtkwave")) {
                        if (system(wv.c_str())) {
                            cerr << "Error: GtkWave failed.";
                        }
                        else {
                            cout << "GtkWave started." << endl;
                        }
                    }
                }
            }
        }
    //}

    //cout << "Simulation ended." << endl;
    return 0;
}

