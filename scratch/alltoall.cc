#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-xpath-routing-helper.h"
#include "ns3/ipv4-tlb.h"
#include "ns3/ipv4-tlb-probing.h"
#include "ns3/ipv4-drb-routing-helper.h"
#include "ns3/random-variable-stream.h"

#include <map>
#include <utility>

extern "C"
{

}

#define LINK_CAPACITY_BASE    1000000000         // 1Gbps
#define LINK_DELAY  MicroSeconds(10)             // 10 MicroSeconds
#define BUFFER_SIZE 600                          // 600 packets
#define PACKET_SIZE 1400

#define RED_QUEUE_MARKING 60 		        	 // 65 Packets (available only in DcTcp)

#define PORT_START 10000
#define PORT_END 50000


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FattreeSimulation");

enum RunMode {
    TLB,
    ECMP,
    DRB,
    PRESTO
};

double poission_gen_interval(double avg_rate)
{
    if (avg_rate > 0)
       return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
       return 0;
}

template<typename T>
T rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
}


int main (int argc, char *argv[])
{

#if 1
    LogComponentEnable ("FattreeSimulation", LOG_LEVEL_INFO);
#endif

    clock_t start, finish;     //定义第一次调用CPU时钟单位的实际，可以理解为定义一个计数器
	double Total_time;        //定义一个double类型的变量，用于存储时间单位
	start = clock(); 

    std::string id = "0";
    std::string runModeStr = "ECMP";
    unsigned randomSeed = 0;
    double load = 0.5;

    // The simulation starting and ending time
    double START_TIME = 0.0;
    double END_TIME = 0.5;

    double FLOW_LAUNCH_END_TIME = 0.2;

    uint32_t serverCount = 8;

    bool dctcpEnabled = false;

    uint32_t alltoall_servers = 64;
    uint32_t appnumbers = 3;
    uint32_t flowSize = 8000;

  

    bool resequenceBuffer = false;

    uint32_t PRESTO_RATIO = 64;

    CommandLine cmd;
    cmd.AddValue ("ID", "Running ID", id);
    cmd.AddValue ("StartTime", "Start time of the simulation", START_TIME);
    cmd.AddValue ("EndTime", "End time of the simulation", END_TIME);
    cmd.AddValue ("FlowLaunchEndTime", "End time of the flow launch period", FLOW_LAUNCH_END_TIME);
    cmd.AddValue ("runMode", "Running mode of this simulation: ECMP, Presto and TLB", runModeStr);
    cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);
    cmd.AddValue ("alltoallServers", "Random seed, 0 for random generated", alltoall_servers);
    cmd.AddValue ("appnumbers", "Random seed, 0 for random generated", appnumbers);
    cmd.AddValue ("flowSize", "Random seed, 0 for random generated", flowSize);
    

    cmd.AddValue ("load", "Load of the network, 0.0 - 1.0", load);
    cmd.AddValue ("enableDcTcp", "Whether to enable DCTCP", dctcpEnabled);

    cmd.AddValue ("resequenceBuffer", "ResequenceBuffer", resequenceBuffer);

    cmd.Parse (argc, argv);

    uint64_t serverEdgeCapacity = 100ul * LINK_CAPACITY_BASE;
    uint64_t edgeAggregationCapacity = 100ul * LINK_CAPACITY_BASE;
    uint64_t aggregationCoreCapacity = 100ul * LINK_CAPACITY_BASE;

    RunMode runMode;
    if (runModeStr.compare ("ECMP") == 0)
    {
        runMode = ECMP;
    }
    else
    {
        NS_LOG_ERROR ("The running mode should be ECMP, Presto, DRB and TLB");
        return 0;
    }

    if (load < 0.0 || load >= 1.0)
    {
        NS_LOG_ERROR ("The network load should within 0.0 and 1.0");
        return 0;
    }

    NS_LOG_INFO ("Config parameters");
    if (dctcpEnabled)
    {
	    NS_LOG_INFO ("Enabling DcTcp");
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDCTCP::GetTypeId ()));
        Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_MODE_BYTES"));
    	Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (PACKET_SIZE));
        Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (BUFFER_SIZE * PACKET_SIZE));
        //Config::SetDefault ("ns3::QueueDisc::Quota", UintegerValue (BUFFER_SIZE));
        Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));
    }

  

    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue(PACKET_SIZE));
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (5)));
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (1));
    Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (5)));
    Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
    Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (80)));
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (160000000));
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (160000000));


    Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_MODE_BYTES"));
    Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (PACKET_SIZE));
    Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (BUFFER_SIZE * PACKET_SIZE));
    Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));

    Config::SetDefault ("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue (true));

    if (resequenceBuffer)
    {
        NS_LOG_INFO ("Enabling Resequence Buffer");
	    Config::SetDefault ("ns3::TcpSocketBase::ResequenceBuffer", BooleanValue (true));
        Config::SetDefault ("ns3::TcpResequenceBuffer::InOrderQueueTimerLimit", TimeValue (MicroSeconds (15)));
        Config::SetDefault ("ns3::TcpResequenceBuffer::SizeLimit", UintegerValue (100));
        Config::SetDefault ("ns3::TcpResequenceBuffer::OutOrderQueueTimerLimit", TimeValue (MicroSeconds (250)));
    }


    uint32_t edgeCount = 32;
    uint32_t aggregationCount = 8;
    serverCount = 8;

    NodeContainer servers;
    NodeContainer edges;
    NodeContainer aggregations;

    servers.Create (serverCount * edgeCount);
    edges.Create (edgeCount);
    aggregations.Create (aggregationCount);

    InternetStackHelper internet;
    Ipv4GlobalRoutingHelper globalRoutingHelper;
    Ipv4ListRoutingHelper listRoutingHelper;
    Ipv4XPathRoutingHelper xpathRoutingHelper;
    Ipv4DrbRoutingHelper drbRoutingHelper;

    if (runMode == ECMP)
    {
	    internet.SetRoutingHelper (globalRoutingHelper);

	    internet.Install (servers);
	    internet.Install (edges);
        internet.Install (aggregations);
    }

    PointToPointHelper p2p;

    if (dctcpEnabled)
    {
        p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (10));
    }
    else
    {
        p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (BUFFER_SIZE));
    }

    Ipv4AddressHelper ipv4;

    ipv4.SetBase ("10.1.0.0", "255.255.0.0");

    TrafficControlHelper tc;

    if (dctcpEnabled)
    {
        tc.SetRootQueueDisc ("ns3::RedQueueDisc", "MinTh", DoubleValue (RED_QUEUE_MARKING * PACKET_SIZE),
                                                  "MaxTh", DoubleValue (RED_QUEUE_MARKING * PACKET_SIZE));
    }

    p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (serverEdgeCapacity)));
    p2p.SetChannelAttribute ("Delay", TimeValue (LINK_DELAY));

    std::map<std::pair<int, int>, uint32_t> edgeToAggregationPath;
    std::map<std::pair<int, int>, uint32_t> aggregationToCorePath;

    std::vector<Ipv4Address> serverAddresses (serverCount * edgeCount);
    std::vector<Ptr<Ipv4TLBProbing> > probings (serverCount * edgeCount);


    NS_LOG_INFO ("Connecting servers to edges");
    for (uint32_t i = 0; i < edgeCount; i++)
    {
        ipv4.NewNetwork ();
        for (uint32_t j = 0; j < serverCount; j++)
        {
            uint32_t uServerIndex = i * serverCount + j;

            NodeContainer nodeContainer = NodeContainer (edges.Get (i), servers.Get (uServerIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            if (dctcpEnabled)
            {
                tc.Install (netDeviceContainer);
            }

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            serverAddresses[uServerIndex] = interfaceContainer.GetAddress (1);

            if (!dctcpEnabled)
            {
                tc.Uninstall (netDeviceContainer);
            }

            NS_LOG_INFO ("Server-" << uServerIndex << " is connected to Edge-" << i
                    << " (" << netDeviceContainer.Get (1)->GetIfIndex () << "<->"
                    << netDeviceContainer.Get (0)->GetIfIndex () << ")");

        }
    }

    p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (edgeAggregationCapacity)));

    NS_LOG_INFO ("Connecting edges to aggregations");
    for (uint32_t i = 0; i < edgeCount; i++)
    {
        for (uint32_t j = 0; j < aggregationCount; j++)
        {
            uint32_t uAggregationIndex = j;

            NodeContainer nodeContainer = NodeContainer (edges.Get (i), aggregations.Get (uAggregationIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            if (dctcpEnabled)
            {
                tc.Install (netDeviceContainer);
            }

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            if (!dctcpEnabled)
            {
                tc.Uninstall (netDeviceContainer);
            }

            std::pair<uint32_t, uint32_t> pathKey = std::make_pair (i, uAggregationIndex);
            edgeToAggregationPath[pathKey] = netDeviceContainer.Get (0)->GetIfIndex ();

            NS_LOG_INFO ("Edge-" << i << " is connected to Aggregation-" << uAggregationIndex
                    << " (" << netDeviceContainer.Get (0)->GetIfIndex () << "<->"
                    << netDeviceContainer.Get (1)->GetIfIndex () << ")");
        }
    }

    NS_LOG_INFO ("Populate global routing tables");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    NS_LOG_INFO ("Initialize random seed: " << randomSeed);
    if (randomSeed == 0)
    {
        srand ((unsigned)time (NULL));
    }
    else
    {
        srand (randomSeed);
    }

    NS_LOG_INFO ("Create applications");

    long flowCount = 0;
    long totalFlowSize = 0;

    NS_LOG_INFO ("Install applications:");

    uint32_t alltoall_servers = 64;
    uint32_t appnumbers = 3;
    uint32_t flowSize = 8000;
    double link_rate = serverEdgeCapacity;
    
    double requestRate = (link_rate*load)/(flowSize*8.0) / (alltoall_servers-1);
	std::cout << "Lambda Per Source: " << requestRate << " flows/s per source\n";
    // double mean_t = 1.0/requestRate ; // seconds
	// std::cout << "Mean Inter-Arrival Time: " << mean_t << " s\n";
	// mean_t *= alltoall_servers-1;
    


    double maxtime = 0;
    for (uint32_t i =0;i < alltoall_servers; i++)
    {
        for (uint32_t j = 0; j < alltoall_servers; j++)
        {
            if (i == j) continue;
            double startTime = START_TIME + poission_gen_interval (requestRate);
            for (uint32_t k = 0; k < appnumbers;k++ )
            {
                uint32_t fromServerIndex = i;
                uint16_t port = rand_range (PORT_START, PORT_END);
                uint32_t destServerIndex = j;

                Ptr<Node> destServer = servers.Get (destServerIndex);
                Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
                Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1, 0);
                Ipv4Address destAddress = destInterface.GetLocal ();

                BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
                
                totalFlowSize += flowSize;
                source.SetAttribute ("SendSize", UintegerValue (PACKET_SIZE));
                source.SetAttribute ("MaxBytes", UintegerValue(flowSize));

                ApplicationContainer sourceApp = source.Install (servers.Get (fromServerIndex));
                sourceApp.Start (Seconds (startTime));
                sourceApp.Stop (Seconds (END_TIME));

                // Install packet sinks
                PacketSinkHelper sink ("ns3::TcpSocketFactory",
                        InetSocketAddress (Ipv4Address::GetAny (), port));
                ApplicationContainer sinkApp = sink.Install (servers. Get (destServerIndex));
                sinkApp.Start (Seconds (startTime));
                sinkApp.Stop (Seconds (END_TIME));
                NS_LOG_INFO ("\tFlow from server: " << fromServerIndex << " to server: "
                    << destServerIndex << " on port: " << port << " with flow size: "
                    << flowSize << " [start time: " << startTime <<"]");

                startTime += poission_gen_interval (requestRate);
                maxtime = maxtime>startTime?maxtime:startTime;
                flowCount ++;
            }
        }
    }
    

    NS_LOG_INFO ("Total flow: " << flowCount);

    NS_LOG_INFO ("Actual average flow size: " << static_cast<double> (totalFlowSize) / flowCount);

    NS_LOG_INFO ("Enabling flow monitor");

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    std::stringstream flowMonitorFilename;

    flowMonitorFilename << id << "-fattree-" << load << "-"  << dctcpEnabled <<"-" << alltoall_servers <<"-";
    if (runMode == ECMP)
    {
        flowMonitorFilename << "ecmp-simulation-";
    }
    

    flowMonitorFilename << randomSeed << ".xml";

    NS_LOG_INFO ("Start simulation");
    Simulator::Stop (Seconds (maxtime));
    Simulator::Run ();

    flowMonitor->SerializeToXmlFile(flowMonitorFilename.str (), true, true);

    Simulator::Destroy ();
    NS_LOG_INFO ("Stop simulation");
    finish = clock();
    Total_time = (double)(finish - start) / CLOCKS_PER_SEC;
    NS_LOG_INFO ("Total time: "<<Total_time);

    return 0;
}

