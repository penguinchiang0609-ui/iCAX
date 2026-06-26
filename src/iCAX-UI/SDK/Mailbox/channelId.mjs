const nilChannelIdPattern = /^0{8}-0{4}-0{4}-0{4}-0{12}$/i;

export function isUsableChannelId(channelId) {
  const value = String(channelId ?? "").trim();
  return value.length > 0 && !nilChannelIdPattern.test(value);
}

export function ensureUsableChannelId(channelId, name = "channelId") {
  if (!isUsableChannelId(channelId)) {
    throw new TypeError(`${name} must be a non-nil channel id`);
  }
}
