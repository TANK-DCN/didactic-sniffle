"""Learn to use cloudlab, and try to config 8 server for homa test."""

#
# NOTE: This code was machine converted. An actual human would not
#       write code like this!
#

# Import the Portal object.
import geni.portal as portal
# Import the ProtoGENI library.
import geni.rspec.pg as pg
# Import the Emulab specific extensions.
import geni.rspec.emulab as emulab
import geni.rspec

# Create a portal object,
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

pc.defineParameter("server_num",
                   "server number",
                   portal.ParameterType.INTEGER, 1)

pc.defineParameter("hardware_type",
                   "Optional physical node type (d710, c8220, etc)",
                   portal.ParameterType.STRING, "")

# Pick your OS.
imageList = [
    ('default', 'Default Image'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD', 'UBUNTU 20.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU18-64-STD', 'UBUNTU 18.04'),
    ('urn:publicid:IDN+apt.emulab.net+image+servelesslegoos-PG0:ubuntu18.mlx4',  'Apt Ubuntu18.04+mlx4'),
    ('urn:publicid:IDN+utah.cloudlab.us+image+servelesslegoos-PG0:homa.node2:0', 'Utah Ubuntu18.04+mlx5')]

pc.defineParameter("osImage", "Select OS image",
                   portal.ParameterType.IMAGE,
                   imageList[3], imageList,
                   longDescription="Most clusters have this set of images, " +
                   "pick your favorite one.")

                   
pc.defineParameter("if_switch",
                   "if need switchr",
                   portal.ParameterType.BOOLEAN, False, [True, False])

pc.defineParameter("phystype", "Switch type",
                   portal.ParameterType.STRING, "dell-s4048",
                   [('mlnx-sn2410', 'Mellanox SN2410'),
                    ('dell-s4048',  'Dell S4048')])

# Optional link speed, normally the resource mapper will choose for you based on node availability
pc.defineParameter("linkSpeed", "Link Speed",portal.ParameterType.INTEGER, 0,
                   [(0,"Any"),(100000,"100Mb/s"),(1000000,"1Gb/s"),(10000000,"10Gb/s"),(25000000,"25Gb/s"),(100000000,"100Gb/s")],
                   advanced=True,
                   longDescription="A specific link speed to use for your lan. Normally the resource " +
                   "mapper will choose for you based on node availability and the optional physical type.")
                   
# For very large lans you might to tell the resource mapper to override the bandwidth constraints
# and treat it a "best-effort"
pc.defineParameter("bestEffort",  "Best Effort", portal.ParameterType.BOOLEAN, False,
                    advanced=True,
                    longDescription="For very large lans, you might get an error saying 'not enough bandwidth.' " +
                    "This options tells the resource mapper to ignore bandwidth and assume you know what you " +
                    "are doing, just give me the lan I ask for (if enough nodes are available).")
                    
# Sometimes you want all of nodes on the same switch, Note that this option can make it impossible
# for your experiment to map.
pc.defineParameter("sameSwitch",  "No Interswitch Links", portal.ParameterType.BOOLEAN, False,
                    advanced=True,
                    longDescription="Sometimes you want all the nodes connected to the same switch. " +
                    "This option will ask the resource mapper to do that, although it might make " +
                    "it imppossible to find a solution. Do not use this unless you are sure you need it!")

# Retrieve the values the user specifies during instantiation.
params = pc.bindParameters()

num = params.server_num

swifaces = []
lan = None
# Add Switch to the request and give it a couple of interfaces
if params.if_switch:
    mysw = request.Switch("mysw");
    mysw.hardware_type = params.phystype
    for i in range(num):
        swifaces.append(mysw.addInterface())
else:
    lan = request.LAN()
    if params.bestEffort:
        lan.best_effort = True
    elif params.linkSpeed > 0:
        lan.bandwidth = params.linkSpeed
    if params.sameSwitch:
        lan.setNoInterSwitchLinks()

nodes = []
for i in range(num):
    node_name = "node" + str(i)
    node = request.RawPC(node_name)
    if params.hardware_type != "":
        node.hardware_type = params.hardware_type

    node.installRootKeys(False, True)
    node.disk_image = params.osImage
    iface = node.addInterface("eth1")
    # ip_addr = "192.168.1."+str(i+1)
    # iface.addAddress(pg.IPv4Address(ip_addr, "255.255.255.0"))
    
    if params.if_switch:
        link_name = "link"+str(i)
        link = request.L1Link(link_name)
        link.addInterface(iface)
        link.addInterface(swifaces[i])
    else:
        lan.addInterface(iface)
        
    node.addService(pg.Execute(shell="sh", command="sudo /local/repository/prepare/prepare.sh"))

    


# Install and execute scripts on the node.
# node.addService(rspec.Install(url="http://example.org/sample.tar.gz", path="/local"))
# node.addService(rspec.Execute(shell="bash", command="/local/example.sh"))


# Print the generated rspec
pc.printRequestRSpec(request)
