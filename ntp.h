#ifndef NTP_H
#define NTP_H

#include <time.h>
#include <RTClib.h>

// ------------------- CONFIGURAÇÕES NTP -------------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -10800;  // UTC-3 (Brasil)
const int daylightOffset_sec = 0;   // Sem horário de verão
const unsigned long NTP_RESYNC_INTERVAL = 3600000UL; // 1 hora

// ------------------- VARIÁVEIS GLOBAIS NTP -------------------
extern bool ntpSynced;
extern unsigned long lastNtpSync;

// ------------------- FUNÇÕES NTP -------------------
/**
 * Sincroniza RTC DS1307 com servidor NTP público
 * @param rtc Referência ao objeto RTC_DS1307
 * @return true se sincronizou com sucesso
 */
bool syncRTCWithNTP(RTC_DS1307& rtc);

/**
 * Verifica e executa sincronização NTP (inicial ou periódica)
 * @param wifiConnected Status da conexão WiFi
 * @param rtcOk Status do RTC
 * @param currentMillis Tempo atual em ms
 * @return true se precisa resync (flag para loop principal)
 */
bool checkNtpSync(bool wifiConnected, bool rtcOk, unsigned long currentMillis);

#endif  // NTP_H
