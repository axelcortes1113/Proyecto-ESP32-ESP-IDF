const axios = require('axios');
const moment = require('moment');

const API_URL = 'http://localhost:3000/api/telemetry'; // Cambia por tu URL pública después

async function generate() {
  const start = moment().subtract(1200 * 3, 'minutes'); // 1200 envíos x 3 min

  for (let i = 0; i < 1200; i++) {
    const ts = start.clone().add(i * 3, 'minutes').unix();
    const temp = 20 + Math.random() * 15;
    const hum = 40 + Math.random() * 30;

    try {
      await axios.post(API_URL, {
        device_id: "esp32-dht11",
        temperature: parseFloat(temp.toFixed(1)),
        humidity: parseFloat(hum.toFixed(1)),
        cpu_cores: 2,
        flash_size_mb: 4,
        free_heap: 180000 + Math.floor(Math.random() * 20000),
        timestamp: ts
      });
      console.log(`Registro ${i + 1}/1200 enviado`);
    } catch (err) {
      console.error('Error:', err.message);
    }
  }
  console.log('¡1200 registros generados!');
}

generate();