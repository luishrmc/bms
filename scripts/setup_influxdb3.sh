# 1. Create directory structure
echo "📁 Creating Influxdb configuration directories..."
mkdir -p config/influxdb3/core/{data,plugins}
mkdir -p config/influxdb3/explorer/{db,config}

# 2. Set permissions
echo "🔐 Setting permissions for Influxdb configuration directories..."
sudo chown -R $USER:$USER config/influxdb3
