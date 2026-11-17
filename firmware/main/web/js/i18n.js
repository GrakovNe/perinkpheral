/* SPDX-License-Identifier: MIT
 * The page in the device's language. English is the page as it is written; a word of it and its
 * Russian here, in the device's own terms (components/words/words.c, the strings of Settings and
 * About). A text with {0} in it is a pattern: the parts are carried over as they are. Names are
 * not translated: Wi-Fi, OTA, SSID, NTP, ESP-IDF, the typefaces.
 *
 * What the page shows is put into the language where it stands: the markup at the start and
 * whatever the scripts or the device's answers put there later, a whole text at a time. What
 * the scripts build of parts goes through t(). An element with translate="no" is left as it is:
 * the log, the device's own names. */
"use strict";

const RUSSIAN = {
  /* the menu and the sections */
  "Apps": "Приложения", "Device": "Устройство", "Control": "Управление", "Settings": "Настройки",
  "Credits": "Лицензии",

  /* apps */
  "Add app": "Добавить приложение", "Sources": "Источники", "Catalog": "Каталог",
  "Installed apps": "Мои приложения", "name": "название",
  "Latest": "Последняя", "{0} - up to date": "{0} - актуальна", "Default": "По умолчанию",
  "Install": "Установить", "Update": "Обновить", "Remove": "Удалить", "Set up": "Настроить",
  "Info": "Сведения", "Loading": "Загрузка", "No apps available.": "Нет доступных приложений.",
  "Update needs firmware {0}": "Для обновления нужна прошивка {0}",
  "Remove {0}? Settings are kept.": "Удалить {0}? Настройки сохранятся.",
  "{0} requests system access.": "{0} запрашивает системный доступ.",
  "{0} trusts its own certificate authority: {1}.": "{0} доверяет своему центру сертификации: {1}.",
  "{0} trusts its own certificate authorities: {1}.": "{0} доверяет своим центрам сертификации: {1}.",
  "Source unavailable: {0}": "Источник недоступен: {0}",
  "Unchanged": "Без изменений", "Not set": "Не задано",
  "Allow pop-ups for this page.": "Разрешите всплывающие окна для этой страницы.",
  "Filled in. Save to apply.": "Заполнено. Сохраните, чтобы применить.",
  "Nothing to fill in.": "Нечего заполнять.",
  "Select a place from the list.": "Выберите место из списка.",
  /* what an application is */
  "Name": "Название", "Version": "Версия", "Connects to": "Подключается к", "Nothing": "Никуда",
  "Updates": "Обновления", "On request": "По запросу", "None": "Нет",
  "Starts with device": "Запуск с устройством", "System access": "Системный доступ",
  "Writes files": "Пишет файлы", "Yes, 32 KB": "Да, 32 КБ", "Yes": "Да", "No": "Нет",
  "Own certificate authorities": "Свои центры сертификации", "Screen": "Экран", "Full": "Весь",
  "With header": "С заголовком", "Set up page": "Страница настройки",
  "Every {0}": "Каждые {0}", "settings kept": "настройки сохранены", "needs firmware {0}": "нужна прошивка {0}",
  "not set": "не задано", "secret": "секрет",

  /* the device */
  "Firmware": "Прошивка", "Built": "Сборка", "Slot": "Слот", "Time": "Время", "Uptime": "Аптайм",
  "Last start": "Последний запуск", "Last fault": "Последний сбой", "Language": "Язык",
  "Network": "Сеть", "IP address": "IP-адрес", "Hostname": "Имя хоста",
  "Memory": "Память", "Free": "Свободно", "Lowest": "Минимум", "Largest block": "Наибольший блок",
  "Errors": "Ошибки", "App area": "Область приложений", "Storage": "Хранилище", "Error": "Ошибка",
  "{0} (verifying)": "{0} (проверка)", "Code {0}": "Код {0}",
  "{0} refused": "отказов: {0}", "{0} failed": "сбоев выделения: {0}",
  "{0}, {1} - {2} start ago": "{0}, {1} - {2} запуск назад",
  "{0}, {1} - {2} starts ago": "{0}, {1} - запусков назад: {2}",
  "Power on": "Включение", "Reset pin": "Кнопка сброса", "Restart": "Перезагрузить",
  "Restarted": "Перезагрузка",
  "Fault": "Сбой", "Interrupt watchdog": "Watchdog прерываний", "Task watchdog": "Watchdog задач",
  "Abort": "Аварийная остановка", "Memory fault": "Ошибка памяти", "Illegal instruction": "Недопустимая инструкция",
  "Breakpoint": "Точка останова", "Stack overflow": "Переполнение стека",
  "Watchdog": "Watchdog", "Wake from sleep": "Выход из сна", "Brownout": "Просадка питания",
  "Online": "В сети", "Connecting": "Подключение", "Setup mode": "Настройка", "Not connected": "Нет сети",
  "{0} KB": "{0} КБ", "{0} of {1}": "{0} из {1}",
  "{0} d {1} h": "{0} д {1} ч", "{0} h {1} min": "{0} ч {1} мин", "{0} min": "{0} мин",
  "{0} h": "{0} ч", "{0} s": "{0} с",
  "Installed": "Установлена", "Check for updates": "Проверить обновления",
  "Custom firmware": "Своя прошивка", "Choose file": "Выбрать файл", "No file chosen": "Файл не выбран",
  "Log": "Журнал", "Refresh": "Обновить", "Follow": "Следить", "Reset": "Сброс",
  "Search": "Поиск", "Clear": "Очистить", "Device log": "Журнал устройства",
  "tag, text, address": "источник, текст, адрес", "{0} lines": "строк: {0}", "restart": "перезапуск",
  "Nothing found.": "Ничего не найдено.", "Empty.": "Пусто.",
  "Factory reset": "Сбросить всё",
  "No releases.": "Нет релизов.", "Up to date.": "Обновлений нет.", "Update to {0}?": "Обновить до {0}?",
  "GitHub unreachable.": "GitHub недоступен.",
  "Factory reset? All settings and app data will be erased.":
    "Сбросить всё? Все настройки и данные приложений будут стёрты.",
  /* a firmware on its way */
  "Updating": "Обновление", "Restarting": "Перезагрузка", "Confirm on the device.": "Подтвердите на устройстве.",
  "Installing.": "Установка.", "Waiting for the device.": "Ожидание устройства.",
  "Back online.": "Снова в сети.", "Device not found.": "Устройство не найдено.",
  "Reload": "Обновить страницу",

  /* control */
  "Up": "Вверх", "Down": "Вниз", "Back": "Назад", "Hold: long press": "Удерживать: долгое нажатие",
  "The device's screen": "Экран устройства",

  /* settings */
  "General": "Основные", "Display": "Экран", "Device name": "Название",
  "Default app": "Приложение по умолчанию", "Menu timeout": "Время ожидания",
  "Power saving": "Экономия батареи", "Disabled": "Выкл", "Always": "Всегда", "Auto": "Авто",
  "Date and time": "Дата и время", "Set time automatically": "Время из сети",
  "NTP server": "NTP-сервер", "Detect": "Определить",
  "Now": "Сейчас", "Set": "Установить", "Time zone": "Часовой пояс", "Custom": "Свой",
  "Font": "Шрифт", "Full refresh": "Полное обновление", "Every update": "Каждое обновление",
  "Every {0} updates": "Каждые {0}", "Never": "Никогда", "Min. interval, s": "Мин. интервал, с",
  "Forced refresh, h": "Принудительное обновление, ч", "Fast refresh": "Быстрое обновление",
  "Dithering": "Дизеринг", "Clock in header": "Часы в заголовке",
  "Change network": "Другая сеть", "Password": "Пароль", "Connect": "Подключить",
  "Glyph packs": "Пакеты глифов", "Catalog unavailable.": "Каталог недоступен.",
  "1 min": "1 мин", "5 min": "5 мин", "15 min": "15 мин", "30 min": "30 мин", "1 h": "1 ч",

  /* the languages, as the device names them */
  "English": "Английский", "Russian": "Русский", "Ukrainian": "Украинский", "Belarusian": "Белорусский",
  "Serbian": "Сербский", "Bulgarian": "Болгарский", "Kazakh": "Казахский", "Greek": "Греческий",
  "German": "Немецкий", "French": "Французский", "Spanish": "Испанский", "Italian": "Итальянский",
  "Portuguese": "Португальский", "Dutch": "Нидерландский", "Polish": "Польский", "Czech": "Чешский",
  "Slovak": "Словацкий", "Hungarian": "Венгерский", "Romanian": "Румынский", "Turkish": "Турецкий",
  "Swedish": "Шведский", "Danish": "Датский", "Norwegian": "Норвежский", "Finnish": "Финский",
  "Lithuanian": "Литовский", "Latvian": "Латышский", "Estonian": "Эстонский",
  "Nordic": "Скандинавские", "Baltic": "Балтийские",
  /* the glyph packs */
  "Extended Latin": "Расширенная латиница", "Cyrillic": "Кириллица",

  /* credits */
  "The project": "Проект", "Author": "Автор", "License": "Лицензия", "Firmware and SDK": "Прошивка и SDK",
  "Applications": "Приложения", "In the firmware": "В прошивке", "Component": "Компонент",
  "Typefaces": "Шрифты", "Typeface": "Шрифт",
  "The Soft Type Project Authors; Cyrillic added": "The Soft Type Project Authors; кириллица добавлена",

  /* dialogs, and what a button says while its work is under way */
  "Cancel": "Отмена", "Close": "Закрыть", "Save": "Сохранить", "Add": "Добавить",
  "Saving": "Сохранение", "Saved": "Сохранено", "Installing": "Установка", "Removing": "Удаление",
  "Removed": "Удалено", "Updated": "Обновлено", "Connected": "Подключено", "Resetting": "Сброс",
  "Setting": "Установка", "Checking": "Проверка",

  /* the device's answers */
  "Device unreachable.": "Устройство недоступно.", "Device busy.": "Устройство занято.",
  "Not confirmed.": "Не подтверждено.", "Not enough memory.": "Недостаточно памяти.",
  "Out of memory.": "Недостаточно памяти.", "Not enough storage.": "Недостаточно места.",
  "Storage error.": "Ошибка хранилища.", "Invalid request.": "Неверный запрос.",
  "Invalid value.": "Неверное значение.", "Invalid value: {0}.": "Неверное значение: {0}.",
  "Invalid manifest.": "Неверный манифест.", "Invalid pack.": "Неверный пакет.",
  "Invalid catalog entry.": "Неверная запись каталога.", "Invalid source URL.": "Неверный адрес источника.",
  "Checksum mismatch.": "Контрольная сумма не совпадает.", "Download failed.": "Загрузка не удалась.",
  "Incompatible firmware version.": "Несовместимая версия прошивки.",
  "Update the firmware.": "Обновите прошивку.", "App not found.": "Приложение не найдено.",
  "Not installed.": "Не установлено.", "No image.": "Нет образа.", "Forbidden.": "Запрещено.",
  "Source not found.": "Источник не найден.", "Too many sources.": "Слишком много источников.",
  "Source unavailable (HTTP {0}).": "Источник недоступен (HTTP {0}).",
  "Time is set automatically.": "Время устанавливается автоматически.",
  "Saved.": "Сохранено.", "Saved. Restarting.": "Сохранено. Перезагрузка.",
  "Saved. Restart to apply.": "Сохранено. Перезагрузите, чтобы применить.",
  "Installed.": "Установлено.", "Installed. Restarting.": "Установлено. Перезагрузка.",
  "Removed.": "Удалено.", "Restarting.": "Перезагрузка.", "Reset. Restarting.": "Сброшено. Перезагрузка.",
};

const LANGUAGES = { ru: RUSSIAN };
const LANGUAGE_KEPT = "perinkpheral-language";

/* The language the page was drawn in: the device's, as it was the last time; a device that
 * says another is shown again (setLanguage). */
const pageLanguage = (() => {
  try { return localStorage.getItem(LANGUAGE_KEPT) || "en"; } catch (error) { return "en"; }
})();
const words = LANGUAGES[pageLanguage] || {};

/* The patterns a whole text is matched against, the longest first. Those with no word of three
 * letters - "{0} h", "{0} of {1}" - are the scripts' own, through t(): matched against whatever
 * stands on the page, they would take any text that ends in " h". */
const patterns = Object.keys(words).filter(text => text.includes("{0}") &&
                                                 /[A-Za-z]{3}/.test(text.replace(/\{\d\}/g, "")))
  .sort((a, b) => b.replace(/\{\d\}/g, "").length - a.replace(/\{\d\}/g, "").length)
  .map(text => [new RegExp("^" + text.replace(/[.*+?^$()|[\]\\]/g, "\\$&")
                                     .replace(/\{(\d)\}/g, "(?<p$1>.+?)") + "$"), words[text], text]);

/* The device's answers that name a setting by its key: translated whatever the key. */
const ANSWERS_WITH_NAMES = new Set(["Invalid value: {0}."]);

const fill = (text, parts) => text.replace(/\{(\d)\}/g, (all, at) => parts[at] === undefined ? all : parts[at]);

/* A text in the page's language: a word of the list, or one made of a pattern and its parts. */
function t(text, ...parts) {
  return fill(words[text] || text, parts);
}

/* A whole text as the page shows it, put into the language: null when it has no translation. */
function translated(text) {
  if (words[text]) return words[text];
  for (const [pattern, into, said] of patterns) {
    const found = text.match(pattern);
    /* A part with no digit is a name somebody gave - "Home min", "Every day" - and is left be;
     * a text that takes a name is the scripts', through t(). The device's own answers are the one
     * exception: they name a setting by its key. */
    if (found && (ANSWERS_WITH_NAMES.has(said) ||
                  Object.values(found.groups).every(part => /\d/.test(part))))
      return into.replace(/\{(\d)\}/g, (all, at) => found.groups["p" + at]);
  }
  return null;
}

const SAID = ["title", "placeholder", "alt", "aria-label"];

function translate(node) {
  if (node.nodeType === Node.TEXT_NODE) {
    if (node.parentElement && node.parentElement.closest("[translate=no], script, style")) return;
    const text = node.nodeValue.trim(), into = text && translated(text);
    if (into && into !== text) node.nodeValue = node.nodeValue.replace(text, into);
    return;
  }
  if (node.nodeType !== Node.ELEMENT_NODE || node.closest("[translate=no]")) return;
  for (const name of SAID) {
    const value = node.getAttribute(name), into = value && translated(value);
    if (into && into !== value) node.setAttribute(name, into);
  }
  for (const child of node.childNodes) translate(child);
}

if (pageLanguage !== "en") {
  document.documentElement.lang = pageLanguage;
  translate(document.body);
  new MutationObserver(changes => {
    for (const change of changes) {
      if (change.type === "childList") change.addedNodes.forEach(translate);
      else translate(change.target);
    }
  }).observe(document.body, { childList: true, subtree: true, characterData: true,
                              attributes: true, attributeFilter: SAID });
}

/* The device's language, as its settings say: the page is read anew in it when it is another. */
function setLanguage(language) {
  const wanted = LANGUAGES[language] ? language : "en";
  if (wanted === pageLanguage) return;
  try { localStorage.setItem(LANGUAGE_KEPT, wanted); } catch (error) { return; }
  location.reload();
}
