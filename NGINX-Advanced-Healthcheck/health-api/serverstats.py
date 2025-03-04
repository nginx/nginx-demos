#
# Sample healthcheck REST API
# Query with:
# curl -s http://127.0.0.1:5000/stats
#

from flask import Flask, jsonify, abort, make_response, request
import os
import psutil
import cpuinfo

app = Flask(__name__)

@app.route('/stats', methods=['GET'])
def get_info():
    load1, load5, load15 = psutil.getloadavg()
    cpu_usage1 = (load1/os.cpu_count()) * 100
    cpu_usage5 = (load5/os.cpu_count()) * 100
    cpu_usage15 = (load15/os.cpu_count()) * 100

    virtual = psutil.virtual_memory()
    swap = psutil.swap_memory()

    output = {}
    output['system'] = {}
    output['system']['bootTime'] = psutil.boot_time()

    output['cpu'] = {}
    output['cpu'] = cpuinfo.get_cpu_info()

    output['cpu']['load'] = {}
    output['cpu']['load']['1minute'] = cpu_usage1
    output['cpu']['load']['5minute'] = cpu_usage5
    output['cpu']['load']['15minute'] = cpu_usage15

    output['virtualMemory'] = {}
    output['virtualMemory']['total'] = virtual.total
    output['virtualMemory']['available'] = virtual.available
    output['virtualMemory']['used'] = virtual.used
    output['virtualMemory']['free'] = virtual.free
    output['virtualMemory']['percent'] = virtual.percent
    output['virtualMemory']['active'] = virtual.active
    output['virtualMemory']['inactive'] = virtual.inactive
    output['virtualMemory']['buffers'] = virtual.buffers
    output['virtualMemory']['cached'] = virtual.cached

    output['swapMemory'] = {}
    output['swapMemory']['total'] = swap.total

    output['disk'] = {}

    for disk in psutil.disk_partitions():
      try:
        mountpoint = disk.mountpoint
        diskstats = psutil.disk_usage(mountpoint)

        output['disk'][mountpoint] = {}
        output['disk'][mountpoint]['total'] = diskstats.total
        output['disk'][mountpoint]['used'] = diskstats.used
        output['disk'][mountpoint]['free'] = diskstats.free
        output['disk'][mountpoint]['percent'] = diskstats.percent
      except Exception:
        pass

    return output
    #return jsonify({'cpu1': cpu_usage1,'cpu5': cpu_usage5,'cpu15': cpu_usage15,'ram': ram_usage})

@app.errorhandler(404)
def not_found(error):
    return make_response(jsonify({'error': 'Not found'}), 404)

if __name__ == '__main__':
    app.run(host='0.0.0.0')
