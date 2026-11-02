import {geometrySnapCandidate} from '../../_shared/workbench/viewport/geometrySnapping.mjs';

export function registrationReferenceCandidate(view,hit,event){
  const pick=view.fiveAxisRegistrationPick;if(!pick)return null;
  const state=view.fiveRegistrationInteraction,placement=view.scene.domain.placements.find(p=>p.id===pick.placementId);
  const candidate=geometrySnapCandidate({viewport:view.viewport,objectId:pick.placementId,mode:pick.mode,expected:view.scene.domain.expected,setupId:placement?.setupId??view.scene.domain.activeSetupId,previous:state?.candidate?.pick===pick?state.candidate:null},hit,event);
  if(!candidate)return null;candidate.pick=pick;
  if(state)state.candidate=candidate;
  return candidate;
}
export function resolveRegistrationCandidate(context, candidate) {
  return candidate.promise ??= (async () => {
    const resolve = async mode => {
      const result = await context.sceneProxy.invoke('CamPlacement.ResolveReference', {
        ...candidate.request, placementId:candidate.request.objectId, query: { ...candidate.request.query, mode },
      });
      if (!candidate.accepts(result)) throw Error('CAD_REFERENCE_TOO_FAR');
      return result;
    };
    if (candidate.pick.mode === 'point') {
      try { return await resolve('endpoint'); }
      catch (error) { if (!/CAD_REFERENCE_(NOT_FOUND|TOO_FAR|AMBIGUOUS)/.test(error.message)) throw error; }
    }
    return resolve(candidate.pick.mode);
  })();
}
