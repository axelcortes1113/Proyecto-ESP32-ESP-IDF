const axios = require('axios');
const moment = require('moment');

const API_URL = 'https://esp32-telemetry.onrender.com/api/telemetry';

async function generate() {
  const start = moment().subtract(1200 * 3, 'minutes');

  for (let i = 0; i < 1200; i++) {
    const ts = start.clone().add(i * 3, 'minutes').unix();
    const temp = 20 + Math.random() * 15;
    const hum = 40 + Math.random() * 30;

    try {
      await axios.post(API_URL, {
        temp: parseFloat(temp.toFixed(1)),
        hum: parseFloat(hum.toFixed(1)),
        timestamp: new Date(ts * 1000).toISOString()   
      });

      console.log(`Registro ${i + 1}/1200 enviado`);

    } catch (err) {
      if (err.response) {
        console.error("STATUS:", err.response.status);
        console.error("BODY:", err.response.data);
      } else {
        console.error("ERROR:", err.message);
      }
    }
  }
  console.log('¡1200 registros generados!');
}

generate();
