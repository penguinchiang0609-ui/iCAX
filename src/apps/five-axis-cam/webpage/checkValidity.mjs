// Editing invalidates an existing result; it cannot create a past inspection.
// Missing and unavailable results stay unverified without becoming expired.
export function invalidateCheckResult(result){
  if(!result||!['Current','Stale'].includes(result.validity))return false;
  Object.assign(result,{validity:'Stale',status:'Unchecked'});return true;
}
