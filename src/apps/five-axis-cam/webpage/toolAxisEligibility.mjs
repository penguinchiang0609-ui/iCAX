// Both cutting and marking paths carry a physical tool axis.
export const supportsToolAxis=cut=>['Cut','Mark'].includes(cut?.machiningKind);
